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
	PLOT_PROP_IN_3D
};

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static GObjectClass *pie_parent_klass;
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

static gboolean
gog_pie_plot_foreach_elem (GogPlot *plot, GogEnumFunc handler, gpointer data)
{
	unsigned i, n;
	GogPiePlot const *model = GOG_PIE_PLOT (plot);
	GogSeries const *series = plot->series->data; /* start with the first */
	GObjectClass *klass = G_OBJECT_GET_CLASS (series);
	GogTheme *theme = gog_object_get_theme (GOG_OBJECT (plot));
	GogStyle *style;
	GODataVector *labels;
	char const *label;
	gboolean free_it;

	if (!model->base.vary_style_by_element)
		return FALSE;

	i = 0;
	n = model->base.cardinality;
	style = gog_style_dup (series->base.style);
	labels = NULL;
	if (series->values[0].data != NULL)
		labels = GO_DATA_VECTOR (series->values[0].data);
	for ( ; i < n ; i++) {
		gog_theme_init_style (theme, style, klass,
			model->base.index_num + i);
		label = (labels != NULL)
			? go_data_vector_get_str (labels, i) : NULL;
		if ((free_it = (label == NULL)))
			label = g_strdup_printf ("%d", i);
		(handler) (i, style, label, data);
		if (free_it)
			g_free ((char *)label);

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

	pie_parent_klass = g_type_class_peek_parent (plot_klass);
	gobject_klass->set_property = gog_pie_plot_set_property;
	gobject_klass->get_property = gog_pie_plot_get_property;

	gog_klass->update	= gog_pie_plot_update;
	gog_klass->type_name	= gog_pie_plot_type_name;
	gog_klass->editor	= gog_pie_plot_editor;
	gog_klass->view_type	= gog_pie_view_get_type ();

	g_object_class_install_property (gobject_klass, PLOT_PROP_INITIAL_ANGLE,
		g_param_spec_float ("initial_angle", "initial_angle",
			"Degrees counter-clockwise from 3 O'Clock.",
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

	{
		static GogSeriesDimDesc dimensions[] = {
			{ N_("Labels"), GOG_SERIES_SUGGESTED, TRUE,
			  GOG_DIM_LABEL, GOG_MS_DIM_CATEGORIES },
			{ N_("Values"), GOG_SERIES_REQUIRED, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_VALUES }
		};
		plot_klass->desc.series.dim = dimensions;
		plot_klass->desc.series.num_dim = G_N_ELEMENTS(dimensions);
	}
	plot_klass->desc.num_series_min = 1;
	plot_klass->desc.num_series_max = 1;
	plot_klass->series_type  = gog_pie_series_get_type ();
	plot_klass->foreach_elem = gog_pie_plot_foreach_elem;
}

static void
gog_pie_plot_init (GogPiePlot *pie)
{
	pie->base.vary_style_by_element = TRUE;
}

GSF_CLASS (GogPiePlot, gog_pie_plot,
	   gog_pie_plot_class_init, gog_pie_plot_init,
	   GOG_PLOT_TYPE)

/*****************************************************************************/

enum {
	RING_PLOT_PROP_0,
	RING_PLOT_PROP_CENTER_SIZE,
};

typedef struct {
	GogPiePlotClass	base;
} GogRingPlotClass;

static GObjectClass *ring_parent_klass;

static void
gog_ring_plot_set_property (GObject *obj, guint param_id,
			    GValue const *value, GParamSpec *pspec)
{
	GogRingPlot *ring = GOG_RING_PLOT (obj);

	switch (param_id) {
	case RING_PLOT_PROP_CENTER_SIZE :
		ring->center_size = g_value_get_float (value);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}

	/* none of the attributes triggers a size change yet.
	 * When we add data labels we'll need it */
	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
}

static void
gog_ring_plot_get_property (GObject *obj, guint param_id,
			    GValue *value, GParamSpec *pspec)
{
	GogRingPlot *ring = GOG_RING_PLOT (obj);

	switch (param_id) {
	case RING_PLOT_PROP_CENTER_SIZE :
		g_value_set_float (value, ring->center_size);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static char const *
gog_ring_plot_type_name (G_GNUC_UNUSED GogObject const *item)
{
	return "PlotRing";
}

extern gpointer gog_ring_plot_pref (GogRingPlot *ring, CommandContext *cc);
static gpointer
gog_ring_plot_editor (GogObject *item,
		      G_GNUC_UNUSED GogDataAllocator *dalloc,
		      CommandContext *cc)
{
	return gog_ring_plot_pref (GOG_RING_PLOT (item), cc);
}

static void
gog_ring_plot_class_init (GogPiePlotClass *pie_plot_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) pie_plot_klass;
	GogObjectClass *gog_klass = (GogObjectClass *) pie_plot_klass;
	GogPlotClass *plot_klass = (GogPlotClass *) pie_plot_klass;

	ring_parent_klass = g_type_class_peek_parent (pie_plot_klass);
	gobject_klass->set_property = gog_ring_plot_set_property;
	gobject_klass->get_property = gog_ring_plot_get_property;

	gog_klass->type_name	= gog_ring_plot_type_name;
	gog_klass->editor	= gog_ring_plot_editor;

	g_object_class_install_property (gobject_klass, RING_PLOT_PROP_CENTER_SIZE,
		g_param_spec_float ("center_size", "center_size",
			"Size of the center hole as a percentage of the radius",
			0, 100., 0.,
			G_PARAM_READWRITE));

	plot_klass->desc.num_series_min = 1;
	plot_klass->desc.num_series_max = G_MAXINT;
}

static void
gog_ring_plot_init (GogRingPlot *ring)
{
	ring->center_size = 0.5;
}

GSF_CLASS (GogRingPlot, gog_ring_plot,
	   gog_ring_plot_class_init, gog_ring_plot_init,
	   GOG_PIE_PLOT_TYPE)

/*****************************************************************************/
typedef GogPlotView		GogPieView;
typedef GogPlotViewClass	GogPieViewClass;

#define MAX_ARC_SEGMENTS 128

static void
gog_pie_view_render (GogView *view, GogViewAllocation const *bbox)
{
	GogPiePlot const *model = GOG_PIE_PLOT (view->model);
	GogPieSeries const *series;
	double real_cx, real_cy, cx, cy, r, dt, tmp, theta, len, *vals, scale;
	double default_sep;
	unsigned elem, j, n, k;
	ArtVpath path [MAX_ARC_SEGMENTS + 4];
	GObjectClass *klass;
	GogTheme *theme = gog_object_get_theme (GOG_OBJECT (model));
	GogStyle *style;
	GSList *ptr;
	unsigned num_series = 0;
	unsigned index;
	double center_radius;
	double center_size = 0.0;
	double r_ext, r_int;

	/* compute number of valid series */
	for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next) {
	  	if (!gog_series_is_valid (GOG_SERIES (ptr->data)))
			continue;
		num_series++;
	}

	if (num_series <=0 )
		return;

	if (GOG_IS_RING_PLOT (model))
		center_size = GOG_RING_PLOT(model)->center_size;

	/* centre things */
	cx = view->allocation.x + view->allocation.w/2.;
	cy = view->allocation.y + view->allocation.h/2.;

	r = view->allocation.h;
	if (r > view->allocation.w)
		r = view->allocation.w;
	r /= 2. * (1. + model->default_separation);
	default_sep = r * model->default_separation;
	center_radius = r * center_size;
	r *= 1. - center_size;

	elem = model->base.index_num;
	index = 1;
	for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next) {
		series = ptr->data;

		if (!gog_series_is_valid (GOG_SERIES (series)))
			continue;

		r_int = center_radius + r * ((double)index - 1.0) / (double)num_series;
		r_ext = center_radius + r * (double)index / (double)num_series;

		style = GOG_STYLED_OBJECT (series)->style;
		if (model->base.vary_style_by_element)  {
			style = gog_style_dup (style);
			klass = G_OBJECT_GET_CLASS (series);
		}
		gog_renderer_push_style (view->renderer, style);

		theta = (model->initial_angle + series->initial_angle) * 2. * M_PI / 360. - M_PI / 2.;

		scale = 2 * M_PI / series->total;
		vals = go_data_vector_get_values (GO_DATA_VECTOR (series->base.values[1].data));
		for (k = 0 ; k < series->base.num_elements; k++) {
			len = fabs (vals[k]) * scale;
			if (!finite (len) || len < 1e-3)
				continue;

			/* only separate the outer ring ? (check this) */
			if (num_series == index) {
				real_cx = cx + default_sep * cos (theta + len/2.);
				real_cy = cy + default_sep * sin (theta + len/2.);
			} else {
				real_cx = cx;
				real_cy = cy;
			}

			n = MAX_ARC_SEGMENTS * len / (2 * M_PI);
			if (n < 12)
				n = 12;
			else if (n > MAX_ARC_SEGMENTS)
				n = MAX_ARC_SEGMENTS;

			n /= 2;

			dt = (double)len / (double)n;
			path[0].code = ART_MOVETO;
			path[0].x = real_cx + r_int * cos (theta);
			path[0].y = real_cy + r_int * sin (theta);
			for (tmp = theta, j = 0; j <= n ; tmp += dt, j++) {
				path[j + 1].code = ART_LINETO;
				path[j + 1].x = real_cx + r_ext * cos (tmp);
				path[j + 1].y = real_cy + r_ext * sin (tmp);
				path[2 * n - j + 2].code = ART_LINETO;
				path[2 * n - j + 2].x = real_cx + r_int * cos (tmp);
				path[2 * n - j + 2].y = real_cy + r_int * sin (tmp);
			}
			path[2 * n + 3].code = ART_END;

			if (model->base.vary_style_by_element)
				gog_theme_init_style (theme, style, klass,
						      model->base.index_num + k);
			gog_renderer_draw_polygon (view->renderer, path,
						   r * len < 5 /* drop outline for thin segments */);

			theta += len;
		}

		gog_renderer_pop_style (view->renderer);
		if (model->base.vary_style_by_element)
			g_object_unref (style);

		index ++;
	}
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
		  GOG_PLOT_VIEW_TYPE)

/*****************************************************************************/

typedef GogSeriesClass GogPieSeriesClass;
enum {
	SERIES_PROP_0,
	SERIES_PROP_INITIAL_ANGLE,
	SERIES_PROP_SEPARATION,
};

static GogObjectClass *series_parent_klass;
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
	int len = 0;
	GogPieSeries *series = GOG_PIE_SERIES (obj);
	unsigned old_num = series->base.num_elements;

	if (series->base.values[1].data != NULL) {
		vals = go_data_vector_get_values (GO_DATA_VECTOR (series->base.values[1].data));
		len = go_data_vector_get_len (
			GO_DATA_VECTOR (series->base.values[1].data));
	}
	series->base.num_elements = len;

	for (total = 0. ; len-- > 0 ;)
		if (finite (vals[len]))
			total += fabs (vals[len]);
	series->total = total;

	/* queue plot for redraw */
	gog_object_request_update (GOG_OBJECT (series->base.plot));
	if (old_num != series->base.num_elements)
		gog_plot_request_cardinality_update (series->base.plot);

	if (series_parent_klass->update)
		series_parent_klass->update (obj);
}

static void
gog_pie_series_class_init (GObjectClass *gobject_klass)
{
	GogObjectClass *gog_klass = (GogObjectClass *)gobject_klass;

	series_parent_klass = g_type_class_peek_parent (gobject_klass);
	gog_klass->update = gog_pie_series_update;

	gobject_klass->set_property = gog_pie_series_set_property;
	gobject_klass->get_property = gog_pie_series_get_property;
	g_object_class_install_property (gobject_klass, SERIES_PROP_INITIAL_ANGLE,
		g_param_spec_float ("initial_angle", "initial_angle",
			"Degrees counter-clockwise from 3 O'Clock.",
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
	gog_ring_plot_get_type ();
}

void
plugin_cleanup (void)
{
}
