/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-pie.c
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
#include "gog-pie.h"
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/gog-theme.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/go-data.h>
#include <goffice/utils/go-color.h>

#include <module-plugin-defs.h>
#include <src/gnumeric-i18n.h>
#include <src/mathfunc.h>
#include <gsf/gsf-impl-utils.h>
#include <math.h>

typedef struct {
	GogPlotClass	base;
} GogPiePlotClass;

enum {
	PLOT_PROP_0,
	PLOT_PROP_INITIAL_ANGLE,
	PLOT_PROP_DEFAULT_SEPARATION,
	PLOT_PROP_IN_3D,
	PLOT_PROP_VARY_STYLE_BY_ELEMENT
};

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static GObjectClass *parent_klass;
static GType gog_pie_view_get_type (void);

static void
gog_pie_plot_set_property (GObject *obj, guint param_id,
			   GValue const *value, GParamSpec *pspec)
{
	GogPiePlot *pie = GOG_PIE_PLOT (obj);

	switch (param_id) {
	case PLOT_PROP_INITIAL_ANGLE :
		pie->initial_angle = g_value_get_float (value);
		break;
	case PLOT_PROP_DEFAULT_SEPARATION :
		pie->default_separation = g_value_get_float (value);
		break;
	case PLOT_PROP_IN_3D :
		pie->in_3d = g_value_get_boolean (value);
		break;
	case PLOT_PROP_VARY_STYLE_BY_ELEMENT:
		pie->vary_style_by_element = g_value_get_boolean (value);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
	/* none of the attributes triggers a size change yet.
	 * When we add data labels we'll need it */
	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
}

static void
gog_pie_plot_get_property (GObject *obj, guint param_id,
			  GValue *value, GParamSpec *pspec)
{
	GogPiePlot *pie = GOG_PIE_PLOT (obj);

	switch (param_id) {
	case PLOT_PROP_INITIAL_ANGLE :
		g_value_set_float (value, pie->initial_angle);
		break;
	case PLOT_PROP_DEFAULT_SEPARATION :
		g_value_set_float (value, pie->default_separation);
		break;
	case PLOT_PROP_IN_3D :
		g_value_set_boolean (value, pie->in_3d);
		break;
	case PLOT_PROP_VARY_STYLE_BY_ELEMENT:
		g_value_set_boolean (value, pie->vary_style_by_element);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static char const *
gog_pie_plot_type_name (G_GNUC_UNUSED GogObject const *item)
{
	return "PlotPie";
}

extern gpointer gog_pie_plot_pref (GogPiePlot *pie, CommandContext *cc);
static gpointer
gog_pie_plot_editor (GogObject *item,
		    G_GNUC_UNUSED GogDataAllocator *dalloc,
		    CommandContext *cc)
{
	return gog_pie_plot_pref (GOG_PIE_PLOT (item), cc);
}

static unsigned
gog_pie_plot_carnality (GogPlot *plot)
{
	GogPiePlot const *pie = GOG_PIE_PLOT (plot);
	GogPieSeries *series;
	unsigned size = 0;
	GSList *ptr;

	if (!pie->vary_style_by_element)
		return 1;
	for (ptr = plot->series ; ptr != NULL ; ptr = ptr->next) {
		series = GOG_PIE_SERIES (ptr->data);
		if (gog_series_is_valid (GOG_SERIES (series)))
			if (size < series->num_elements)
				size = series->num_elements;
	}
	return size;
}

static gboolean
gog_pie_plot_foreach_elem (GogPlot *plot, GogEnumFunc handler, gpointer data)
{
	GogStyle *style;
	unsigned i, n;
	GogPiePlot const *model = GOG_PIE_PLOT (plot);

	if (!model->vary_style_by_element)
		return FALSE;

	style = g_object_new (GOG_STYLE_TYPE, NULL);
	style->outline.width = 0.;
	style->outline.color = RGBA_BLACK;
	/* use the shared first dimension for labels */
	i = model->base.index_num;
	n = i + model->base.carnality;
	for ( ; i < n ; i++) {
		gog_theme_init_style (style, model->base.index_num + i);
		(handler) (i, style, "gog", data);
	}
	g_object_unref (style);
	return TRUE;
}

static void
gog_pie_plot_update (GogObject *obj)
{
	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
}

static void
gog_pie_plot_class_init (GogPlotClass *plot_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) plot_klass;
	GogObjectClass *gog_klass = (GogObjectClass *) plot_klass;

	parent_klass = g_type_class_peek_parent (plot_klass);
	gobject_klass->set_property = gog_pie_plot_set_property;
	gobject_klass->get_property = gog_pie_plot_get_property;

	gog_klass->update	= gog_pie_plot_update;
	gog_klass->type_name	= gog_pie_plot_type_name;
	gog_klass->editor	= gog_pie_plot_editor;
	gog_klass->view_type	= gog_pie_view_get_type ();

	g_object_class_install_property (gobject_klass, PLOT_PROP_INITIAL_ANGLE,
		g_param_spec_float ("initial_angle", "initial_angle",
			"Degrees counterclockwise from 3oclock.",
			0, G_MAXFLOAT, 0.,
			G_PARAM_READWRITE));
	g_object_class_install_property (gobject_klass, PLOT_PROP_DEFAULT_SEPARATION,
		g_param_spec_float ("default_separation", "default_separation",
			"Default amount a slice is extended as a percentage of the radius",
			0, G_MAXFLOAT, 0.,
			G_PARAM_READWRITE));
	g_object_class_install_property (gobject_klass, PLOT_PROP_IN_3D,
		g_param_spec_boolean ("in_3d", "in_3d",
			"Draw 3d wedges",
			FALSE,
			G_PARAM_READWRITE));
	g_object_class_install_property (gobject_klass, PLOT_PROP_VARY_STYLE_BY_ELEMENT,
		g_param_spec_boolean ("vary_style_by_element", "vary_style_by_element",
			"Use a different style for each segments",
			FALSE,
			G_PARAM_READWRITE));

	{
		static GogSeriesDimDesc dimensions[] = {
			{ N_("Labels"), GOG_SERIES_SUGGESTED,  TRUE, GOG_DIM_LABEL,
			  GOG_AXIS_NONE, GOG_MS_DIM_CATEGORIES },
			{ N_("Values"), GOG_SERIES_REQUIRED, FALSE, GOG_DIM_VALUE,
			  GOG_AXIS_NONE, GOG_MS_DIM_VALUES }
		};
		plot_klass->desc.series.dim = dimensions;
		plot_klass->desc.series.num_dim = G_N_ELEMENTS(dimensions);
	}
	plot_klass->desc.num_axis = 0;
	plot_klass->desc.num_series_min = plot_klass->desc.num_series_max = 1;
	plot_klass->series_type  = gog_pie_series_get_type ();
	plot_klass->carnality    = gog_pie_plot_carnality;
	plot_klass->foreach_elem = gog_pie_plot_foreach_elem;
}

static void
gog_pie_plot_init (GogPiePlot *pie)
{
	pie->vary_style_by_element = TRUE;
}

GSF_CLASS (GogPiePlot, gog_pie_plot,
	   gog_pie_plot_class_init, gog_pie_plot_init,
	   GOG_PLOT_TYPE)

/*****************************************************************************/
typedef GogView		GogPieView;
typedef GogViewClass	GogPieViewClass;

#define MAX_ARC_SEGMENTS 128

static void
gog_pie_view_render (GogView *view, GogViewAllocation const *bbox)
{
	GogPiePlot const *model = GOG_PIE_PLOT (view->model);
	GogPieSeries const *series;
	double cx, cy, r, tmp, theta, len, *vals, scale;
	unsigned elem, j, n, k;
	ArtVpath path [MAX_ARC_SEGMENTS + 4];
	GogStyle *style = g_object_new (GOG_STYLE_TYPE, NULL);
	GSList *ptr;

	style->outline.width = 0.;
	style->outline.color = RGBA_BLACK;
	gog_renderer_push_style (view->renderer, style);

	/* centre things */
	cx = view->allocation.x + view->allocation.w/2.;
	cy = view->allocation.y + view->allocation.h/2.;

	r = view->allocation.h;
	if (r > view->allocation.w)
		r = view->allocation.w;
	r /= 2.;

	elem = model->base.index_num;
	for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next) {
		series = ptr->data;

		if (!gog_series_is_valid (GOG_SERIES (series)))
			continue;

		if (!model->vary_style_by_element)
			gog_theme_init_style (style, elem++);

		theta = (model->initial_angle + series->initial_angle) * 2. * M_PI / 360.;

		scale = 2 * M_PI / series->total;
		vals = go_data_vector_get_values (GO_DATA_VECTOR (series->base.values[1].data));
		for (k = 0 ; k < series->num_elements; k++) {
			len = fabs (vals[k]) * scale;
			if (!finite (len) || len < 1e-3)
				continue;
			n = MAX_ARC_SEGMENTS * len / (2 * M_PI);
			if (n < 12)
				n = 12;
			else if (n > MAX_ARC_SEGMENTS)
				n = MAX_ARC_SEGMENTS;

			path[0].code = ART_MOVETO;
			path[0].x = cx;
			path[0].y = cy; 
			for (j = 0; j <= n ;) {
				tmp = theta + (double)(j++ * len) / (double)n;
				path[j].code = ART_LINETO;
				path[j].x = cx + r * cos (tmp);
				path[j].y = cy + r * sin (tmp); 
			}
			j++;
			path[j].code = ART_LINETO;
			path[j].x = cx;
			path[j].y = cy; 
			path[j+1].code = ART_END;

			theta += len;
			if (model->vary_style_by_element)
				gog_theme_init_style (style, model->base.index_num + k);
			gog_renderer_draw_polygon (view->renderer, path,
				r * len < 5 /* drop outline for thin segments */);
		}
	}

	gog_renderer_pop_style (view->renderer);
	g_object_unref (style);
}

static GogObject *
gog_pie_view_point (GogView *view, double x, double y)
{
	double r = view->allocation.h;
	if (r > view->allocation.w)
		r = view->allocation.w;
	r /= 2.;
	x -= view->allocation.x + view->allocation.w/2.;
	y -= view->allocation.y + view->allocation.h/2.;
	return ((x*x + y*y) <= (r*r)) ? view->model : NULL;
}

static void
gog_pie_view_class_init (GogViewClass *view_klass)
{
	view_klass->render = gog_pie_view_render;
	view_klass->point  = gog_pie_view_point;
}

static GSF_CLASS (GogPieView, gog_pie_view,
		  gog_pie_view_class_init, NULL,
		  GOG_VIEW_TYPE)

/*****************************************************************************/

typedef GogSeriesClass GogPieSeriesClass;
enum {
	SERIES_PROP_0,
	SERIES_PROP_INITIAL_ANGLE,
	SERIES_PROP_SEPARATION,
};

static void
gog_pie_series_set_property (GObject *obj, guint param_id,
			     GValue const *value, GParamSpec *pspec)
{
	GogPieSeries *pie = GOG_PIE_SERIES (obj);

	switch (param_id) {
	case SERIES_PROP_INITIAL_ANGLE :
		pie->initial_angle = g_value_get_float (value);
		break;
	case SERIES_PROP_SEPARATION :
		pie->separation = g_value_get_float (value);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
	/* none of the attributes triggers a size change yet.
	 * When we add data labels we'll need it */
	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
}

static void
gog_pie_series_get_property (GObject *obj, guint param_id,
			  GValue *value, GParamSpec *pspec)
{
	GogPieSeries *pie = GOG_PIE_SERIES (obj);

	switch (param_id) {
	case SERIES_PROP_INITIAL_ANGLE :
		g_value_set_float (value, pie->initial_angle);
		break;
	case SERIES_PROP_SEPARATION :
		g_value_set_float (value, pie->separation);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gog_pie_series_update (GogObject *obj)
{
	double *vals, total;
	int len;
	GogPieSeries *series = GOG_PIE_SERIES (obj);
	unsigned old_num = series->num_elements;

	vals = go_data_vector_get_values (GO_DATA_VECTOR (series->base.values[1].data));
	series->num_elements = len = go_data_vector_get_len (
		GO_DATA_VECTOR (series->base.values[1].data));

	for (total = 0. ; len-- > 0 ;)
		if (finite (vals[len]))
			total += fabs (vals[len]);
	series->total = total;

	/* queue plot for redraw */
	gog_object_request_update (GOG_OBJECT (series->base.plot));
	if (old_num != series->num_elements)
		gog_plot_request_carnality_update (series->base.plot);
}

static void
gog_pie_series_class_init (GObjectClass *gobject_klass)
{
	GogObjectClass *gog_klass = (GogObjectClass *)gobject_klass;

	gog_klass->update = gog_pie_series_update;

	gobject_klass->set_property = gog_pie_series_set_property;
	gobject_klass->get_property = gog_pie_series_get_property;
	g_object_class_install_property (gobject_klass, SERIES_PROP_INITIAL_ANGLE,
		g_param_spec_float ("initial_angle", "initial_angle",
			"Degrees counterclockwise from 3oclock.",
			0, G_MAXFLOAT, 0.,
			G_PARAM_READWRITE));
	g_object_class_install_property (gobject_klass, SERIES_PROP_SEPARATION,
		g_param_spec_float ("separation", "separation",
			"Default amount a slice is extended as a percentage of the radius",
			0, G_MAXFLOAT, 0.,
			G_PARAM_READWRITE));
}

GSF_CLASS (GogPieSeries, gog_pie_series,
	   gog_pie_series_class_init, NULL,
	   GOG_SERIES_TYPE)

void
plugin_init (void)
{
	gog_pie_plot_get_type ();
}

void
plugin_cleanup (void)
{
}
