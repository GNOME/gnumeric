/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-xy.c
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
#include "gog-xy.h"
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-axis.h>
#include <goffice/graph/go-data.h>
#include <goffice/utils/go-color.h>
#include <goffice/utils/go-marker.h>

#include <module-plugin-defs.h>
#include <src/gnumeric-i18n.h>
#include <src/mathfunc.h>
#include <gsf/gsf-impl-utils.h>
#include <math.h>

typedef struct {
	GogPlotClass	base;
} GogXYPlotClass;

enum {
	GOG_XY_PROP_0,
	GOG_XY_PROP_DEFAULT_STYLE_HAS_MARKERS,
	GOG_XY_PROP_DEFAULT_STYLE_HAS_LINES
};

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static GogObjectClass *xy_parent_klass;
static GType gog_xy_view_get_type (void);
static GType gog_xy_series_get_type (void);

#define GOG_XY_PLOT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GOG_XY_PLOT_TYPE, GogXYPlotClass))

static char const *
gog_xy_plot_type_name (G_GNUC_UNUSED GogObject const *item)
{
	/* xgettext : the base for how to name scatter plot objects
	 * eg The 2nd plot in a chart will be called
	 * 	PlotXY2 */
	return N_("PlotXY");
}

static void
gog_xy_plot_update (GogObject *obj)
{
	GogXYPlot *model = GOG_XY_PLOT (obj);
	GogXYSeries const *series;
	double x_min, x_max, y_min, y_max, tmp_min, tmp_max;
	GSList *ptr;
	gboolean is_discrete = FALSE;

	x_min = y_min =  DBL_MAX;
	x_max = y_max = -DBL_MAX;
	for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next) {
		series = ptr->data;
		if (!gog_series_is_valid (GOG_SERIES (series)))
			continue;

		go_data_vector_get_minmax (GO_DATA_VECTOR (
			series->base.values[1].data), &tmp_min, &tmp_max);
		if (y_min > tmp_min) y_min = tmp_min;
		if (y_max < tmp_max) y_max = tmp_max;

		if (series->base.values[0].data != NULL) {
			go_data_vector_get_minmax (GO_DATA_VECTOR (
				series->base.values[0].data), &tmp_min, &tmp_max);

			if (!finite (tmp_min) || !finite (tmp_max) ||
			    tmp_min > tmp_max) {
				tmp_min = 0;
				tmp_max = go_data_vector_get_len (
					GO_DATA_VECTOR (series->base.values[1].data));

				is_discrete = TRUE;
			}
		} else {
			tmp_min = 0;
			tmp_max = go_data_vector_get_len (
				GO_DATA_VECTOR (series->base.values[1].data));
			is_discrete = TRUE;
		}

		if (x_min > tmp_min) x_min = tmp_min;
		if (x_max < tmp_max) x_max = tmp_max;
	}

	if (model->x.minimum != x_min || model->x.maximum != x_max) {
		model->x.minimum = x_min;
		model->x.maximum = x_max;
		gog_axis_bound_changed (model->base.axis[0], GOG_OBJECT (model));
	}
	if (model->y.minimum != y_min || model->y.maximum != y_max) {
		model->y.minimum = y_min;
		model->y.maximum = y_max;
		gog_axis_bound_changed (model->base.axis[1], GOG_OBJECT (model));
	}
	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
	if (xy_parent_klass->update)
		xy_parent_klass->update (obj);
}

static GogAxisSet
gog_xy_plot_axis_set_pref (GogPlot const *plot)
{
	return GOG_AXIS_SET_XY;
}

static gboolean
gog_xy_plot_axis_set_is_valid (GogPlot const *plot, GogAxisSet type)
{
	return type == GOG_AXIS_SET_XY;
}

static gboolean
gog_xy_plot_axis_set_assign (GogPlot *plot, GogAxisSet type)
{
	return type == GOG_AXIS_SET_XY;
}

static GOData *
gog_xy_plot_axis_bounds (GogPlot *plot, GogAxisType axis,
			 double *minima, double *maxima,
			 double *logical_min, double *logical_max,
			 gboolean *is_discrete)
{
	GogXYPlot *model = GOG_XY_PLOT (plot);

	if (axis == GOG_AXIS_X) {
		GSList *ptr;

		*minima = model->x.minimum;
		*maxima = model->x.maximum;
		*is_discrete = model->x.minimum > model->x.maximum ||
			!finite (model->x.minimum) ||
			!finite (model->x.maximum);

		for (ptr = plot->series; ptr != NULL ; ptr = ptr->next)
			if (gog_series_is_valid (GOG_SERIES (ptr->data)))
				return GOG_SERIES (ptr->data)->values[0].data;
		return NULL;
	} 
	
	if (axis == GOG_AXIS_Y) {
		*minima = model->y.minimum;
		*maxima = model->y.maximum;
	}
	return NULL;
}

static void
gog_xy_set_property (GObject *obj, guint param_id,
		     GValue const *value, GParamSpec *pspec)
{
	GogXYPlot *xy = GOG_XY_PLOT (obj);
	switch (param_id) {
	case GOG_XY_PROP_DEFAULT_STYLE_HAS_MARKERS:
		xy->default_style_has_markers = g_value_get_boolean (value);
		break;
	case GOG_XY_PROP_DEFAULT_STYLE_HAS_LINES:
		xy->default_style_has_lines = g_value_get_boolean (value);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}
static void
gog_xy_get_property (GObject *obj, guint param_id,
		     GValue *value, GParamSpec *pspec)
{
	GogXYPlot const *xy = GOG_XY_PLOT (obj);
	switch (param_id) {
	case GOG_XY_PROP_DEFAULT_STYLE_HAS_MARKERS:
		g_value_set_boolean (value, xy->default_style_has_markers);
		break;
	case GOG_XY_PROP_DEFAULT_STYLE_HAS_LINES:
		g_value_set_boolean (value, xy->default_style_has_lines);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}
static void
gog_xy_plot_class_init (GogPlotClass *plot_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) plot_klass;
	GogObjectClass *gog_klass = (GogObjectClass *) plot_klass;

	xy_parent_klass = g_type_class_peek_parent (plot_klass);

	gobject_klass->set_property = gog_xy_set_property;
	gobject_klass->get_property = gog_xy_get_property;

	g_object_class_install_property (gobject_klass, GOG_XY_PROP_DEFAULT_STYLE_HAS_MARKERS,
		g_param_spec_boolean ("default-style-has-markers", NULL,
			"Should the default style of a series include markers",
			TRUE, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, GOG_XY_PROP_DEFAULT_STYLE_HAS_LINES,
		g_param_spec_boolean ("default-style-has-lines", NULL,
			"Should the default style of a series include lines",
			TRUE, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));

	gog_klass->update	= gog_xy_plot_update;
	gog_klass->type_name	= gog_xy_plot_type_name;
	gog_klass->view_type	= gog_xy_view_get_type ();

	{
		static GogSeriesDimDesc dimensions[] = {
			{ N_("X"), GOG_SERIES_SUGGESTED, FALSE,
			  GOG_DIM_INDEX, GOG_MS_DIM_CATEGORIES },
			{ N_("Y"), GOG_SERIES_REQUIRED, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_VALUES }
		};
		plot_klass->desc.series.dim = dimensions;
		plot_klass->desc.series.num_dim = G_N_ELEMENTS (dimensions);
		plot_klass->desc.series.style_fields = GOG_STYLE_LINE | GOG_STYLE_MARKER;
		plot_klass->series_type = gog_xy_series_get_type ();
	}
	plot_klass->desc.num_series_min = 1;
	plot_klass->desc.num_series_max = G_MAXINT;
	plot_klass->series_type  = gog_xy_series_get_type ();
	plot_klass->axis_set_pref     = gog_xy_plot_axis_set_pref;
	plot_klass->axis_set_is_valid = gog_xy_plot_axis_set_is_valid;
	plot_klass->axis_set_assign   = gog_xy_plot_axis_set_assign;
	plot_klass->axis_bounds	      = gog_xy_plot_axis_bounds;
}

static void
gog_xy_plot_init (GogXYPlot *xy)
{
	xy->base.vary_style_by_element = FALSE;
	xy->default_style_has_markers = TRUE;
	xy->default_style_has_lines = TRUE;
}

GSF_CLASS (GogXYPlot, gog_xy_plot,
	   gog_xy_plot_class_init, gog_xy_plot_init,
	   GOG_PLOT_TYPE)

/*****************************************************************************/
typedef GogPlotView		GogXYView;
typedef GogPlotViewClass	GogXYViewClass;

static void
gog_xy_view_render (GogView *view, GogViewAllocation const *bbox)
{
	GogXYPlot const *model = GOG_XY_PLOT (view->model);
	GogXYSeries const *series;
	unsigned i, n, tmp;
	GSList *ptr;
	double const *y_vals, *x_vals = NULL;
	double x, y, x_min, x_max, x_off, x_scale, y_min, y_max, y_off, y_scale;
	double prev_x, prev_y;
	ArtVpath	path[3];
	GogStyle const *style;
	gboolean valid, prev_valid, show_marks, show_lines;

	if (!gog_axis_get_bounds (model->base.axis[0], &x_min, &x_max))
		return;
	x_scale = view->residual.w / (x_max - x_min);
	x_off   = view->residual.x - x_scale * x_min;
	x_min   = view->residual.x;
	x_max   = x_min + view->residual.w;

	if (!gog_axis_get_bounds (model->base.axis[1], &y_min, &y_max))
		return;
	y_scale = - view->residual.h / (y_max - y_min);
	y_off   =   view->residual.y + view->residual.h - y_scale * y_min;
	y_min   = view->residual.y;
	y_max   = y_min + view->residual.h;

	path[0].code = ART_MOVETO;
	path[1].code = ART_LINETO;
	path[2].code = ART_END;
	for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next) {
		series = ptr->data;

		if (!gog_series_is_valid (GOG_SERIES (series)))
			continue;

		y_vals = go_data_vector_get_values (
			GO_DATA_VECTOR (series->base.values[1].data));
		n = go_data_vector_get_len (
			GO_DATA_VECTOR (series->base.values[1].data));
		if (series->base.values[0].data) {
			x_vals = go_data_vector_get_values (
				GO_DATA_VECTOR (series->base.values[0].data));
			tmp = go_data_vector_get_len (
				GO_DATA_VECTOR (series->base.values[0].data));
			if (n > tmp)
				n = tmp;
		}

		if (n <= 0)
			continue;

		style = GOG_STYLED_OBJECT (series)->style;
		show_marks = gog_style_is_marker_visible (style);
		show_lines = gog_style_is_line_visible (style);
		if (!show_marks && !show_lines)
			continue;

		prev_valid = FALSE;
		gog_renderer_push_style (view->renderer, style);
		for (i = 1 ; i <= n ; i++) {
			x = x_vals ? *x_vals++ : i;
			y = *y_vals++;
			valid = !isnan (y) && !isnan (x);
			if (valid) {
				/* We are checking with finite here because isinf
				   if not available everywhere.  Note, that NANs
				   have been ruled out.  */
				if (!finite (y))
					y = 0; /* excel is just sooooo consistent */
				if (!finite (x))
					x = i;
#warning "move map into axis"
				x = x_off + x_scale * x;
				y = y_off + y_scale * y;
				if (prev_valid && show_lines) {
					path[0].x = prev_x;
					path[0].y = prev_y;
					path[1].x = x;
					path[1].y = y;
					gog_renderer_draw_path (view->renderer, path, NULL);
				}
			}

			/* draw marker after line */
			if (prev_valid && show_marks &&
			    x_min <= prev_x && prev_x <= x_max &&
			    y_min <= prev_y && prev_y <= y_max)
				gog_renderer_draw_marker (view->renderer, prev_x, prev_y);

			prev_x = x;
			prev_y = y;
			prev_valid = valid;
		}

		/* draw marker after line */
		if (prev_valid && show_marks &&
		    x_min <= prev_x && prev_x <= x_max &&
		    y_min <= prev_y && prev_y <= y_max)
			gog_renderer_draw_marker (view->renderer, x, y);

		gog_renderer_pop_style (view->renderer);
	}
}

static GogObject *
gog_xy_view_point (GogView *view, double x, double y)
{
	return NULL;
}

static void
gog_xy_view_class_init (GogViewClass *view_klass)
{
	view_klass->render = gog_xy_view_render;
	view_klass->point  = gog_xy_view_point;
}

static GSF_CLASS (GogXYView, gog_xy_view,
		  gog_xy_view_class_init, NULL,
		  GOG_PLOT_VIEW_TYPE)

/*****************************************************************************/

typedef GogSeriesClass GogXYSeriesClass;
enum {
	SERIES_PROP_0,
};

static GogStyledObjectClass *series_parent_klass;

static void
gog_xy_series_update (GogObject *obj)
{
	double *x_vals = NULL, *y_vals = NULL;
	int x_len = 0, y_len = 0;
	GogXYSeries *series = GOG_XY_SERIES (obj);
	unsigned old_num = series->base.num_elements;

	if (series->base.values[1].data != NULL) {
		y_vals = go_data_vector_get_values (GO_DATA_VECTOR (series->base.values[1].data));
		y_len = go_data_vector_get_len (
			GO_DATA_VECTOR (series->base.values[1].data));
	}
	if (series->base.values[0].data != NULL) {
		x_vals = go_data_vector_get_values (GO_DATA_VECTOR (series->base.values[0].data));
		x_len = go_data_vector_get_len (
			GO_DATA_VECTOR (series->base.values[0].data));
	} else
		x_len = y_len;
	series->base.num_elements = MIN (x_len, y_len);

	/* queue plot for redraw */
	gog_object_request_update (GOG_OBJECT (series->base.plot));
	if (old_num != series->base.num_elements)
		gog_plot_request_cardinality_update (series->base.plot);

	if (series_parent_klass->base.update)
		series_parent_klass->base.update (obj);
}

static void
gog_xy_series_init_style (GogStyledObject *gso, GogStyle *style)
{
	GogSeries *series = GOG_SERIES (gso);
	GogXYPlot const *xy;

	series_parent_klass->init_style (gso, style);
	if (series->plot == NULL)
		return;
	xy = GOG_XY_PLOT (series->plot);

	if (style->marker.auto_shape && !xy->default_style_has_markers) {
		GOMarker *m = go_marker_new ();
		go_marker_set_shape (m, GO_MARKER_NONE);
		gog_style_set_marker (style, m);
		style->marker.auto_shape = FALSE;
	}
	if (style->line.auto_color && !xy->default_style_has_lines) {
		style->line.color = 0;
		style->line.auto_color = FALSE;
	}
}

static void
gog_xy_series_class_init (GogStyledObjectClass *gso_klass)
{
	GogObjectClass *gog_klass = (GogObjectClass *)gso_klass;

	series_parent_klass = g_type_class_peek_parent (gso_klass);
	gog_klass->update	= gog_xy_series_update;
	gso_klass->init_style	= gog_xy_series_init_style;
}

static GSF_CLASS (GogXYSeries, gog_xy_series,
	   gog_xy_series_class_init, NULL,
	   GOG_SERIES_TYPE)

void
plugin_init (void)
{
	gog_xy_plot_get_type ();
}

void
plugin_cleanup (void)
{
}
