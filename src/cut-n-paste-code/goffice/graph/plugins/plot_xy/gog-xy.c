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

#include <module-plugin-defs.h>
#include <src/gnumeric-i18n.h>
#include <src/mathfunc.h>
#include <gsf/gsf-impl-utils.h>
#include <math.h>

typedef struct {
	GogPlotClass	base;
} GogXYPlotClass;

enum {
	PLOT_PROP_0,
};

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static GogObjectClass *xy_parent_klass;
static GType gog_xy_view_get_type (void);

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

	x_min = y_min = DBL_MAX;
	x_max = y_max = DBL_MIN;
	for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next) {
		series = ptr->data;
		if (!gog_series_is_valid (GOG_SERIES (series)))
			continue;

		go_data_vector_get_minmax (GO_DATA_VECTOR (
			series->base.values[0].data), &tmp_min, &tmp_max);
		if (x_min > tmp_min) x_min = tmp_min;
		if (x_max < tmp_max) x_max = tmp_max;

		go_data_vector_get_minmax (GO_DATA_VECTOR (
			series->base.values[1].data), &tmp_min, &tmp_max);
		if (y_min > tmp_min) y_min = tmp_min;
		if (y_max < tmp_max) y_max = tmp_max;
	}

	if (model->x.minimum != x_min || model->x.maximum != x_max)
		gog_axis_bound_changed (model->base.axis[0], GOG_OBJECT (model),
			(model->x.minimum = x_min), (model->x.maximum = x_max));
	if (model->y.minimum != y_min || model->y.maximum != y_max)
		gog_axis_bound_changed (model->base.axis[1], GOG_OBJECT (model),
			(model->y.minimum = y_min), (model->y.maximum = y_max));

	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
	if (xy_parent_klass->update)
		xy_parent_klass->update (obj);
}

static GogAxisSet
gog_plot_xy_axis_set_pref (GogPlot const *plot)
{
	return GOG_AXIS_SET_XY;
}

static gboolean
gog_plot_xy_axis_set_is_valid (GogPlot const *plot, GogAxisSet type)
{
	return type == GOG_AXIS_SET_XY;
}

static gboolean
gog_plot_xy_axis_set_assign (GogPlot *plot, GogAxisSet type)
{
	return type == GOG_AXIS_SET_XY;
}

static void
gog_xy_plot_class_init (GogPlotClass *plot_klass)
{
	GogObjectClass *gog_klass = (GogObjectClass *) plot_klass;

	xy_parent_klass = g_type_class_peek_parent (plot_klass);

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
	}
	plot_klass->desc.num_series_min = 1;
	plot_klass->desc.num_series_max = G_MAXINT;
	plot_klass->series_type  = gog_xy_series_get_type ();
	plot_klass->axis_set_pref     = gog_plot_xy_axis_set_pref;
	plot_klass->axis_set_is_valid = gog_plot_xy_axis_set_is_valid;
	plot_klass->axis_set_assign   = gog_plot_xy_axis_set_assign;
}

static void
gog_xy_plot_init (GogXYPlot *xy)
{
	xy->base.vary_style_by_element = TRUE;
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
	unsigned n, tmp;
	GSList *ptr;
	double const *x_vals, *y_vals;
	double x, y, x_min, x_max, x_off, x_scale, y_min, y_max, y_off, y_scale;
	ArtVpath	path[3];
	GogStyle const *style;
	gboolean prev_valid, show_marks, show_lines;

	if (!gog_axis_get_bounds (model->base.axis[0], &x_min, &x_max) ||
	    x_min >= x_max)
		return;
	x_scale = view->residual.w / (x_max - x_min);
	x_off   = view->residual.x - x_scale * x_min;
	x_min   = view->residual.x;
	x_max   = x_min + view->residual.w;

	if (!gog_axis_get_bounds (model->base.axis[1], &y_min, &y_max) ||
	    y_min >= y_max)
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

		x_vals = go_data_vector_get_values (
			GO_DATA_VECTOR (series->base.values[0].data));
		y_vals = go_data_vector_get_values (
			GO_DATA_VECTOR (series->base.values[1].data));
		n = go_data_vector_get_len (
			GO_DATA_VECTOR (series->base.values[0].data));
		tmp = go_data_vector_get_len (
			GO_DATA_VECTOR (series->base.values[1].data));

		if (n > tmp)
			n = tmp;
		if (tmp <= 0)
			continue;

		style = GOG_STYLED_OBJECT (series)->style;
		show_marks = gog_style_is_marker_visible (style);
		show_lines = gog_style_is_line_visible (style);
		if (!show_marks && !show_lines)
			continue;

		prev_valid = FALSE;
		gog_renderer_push_style (view->renderer, style);
		while (n-- > 0) {
			x = *x_vals++;
			y = *y_vals++;
			if (finite (x) && finite (y)) {
#warning move map into axis
				x = x_off + x_scale * x;
				y = y_off + y_scale * y;
				if (show_marks)
					gog_renderer_draw_marker (view->renderer, x, y);

				if (show_lines) {
					if (prev_valid) {
						path[0].x = x;
						path[0].y = y;
						gog_renderer_draw_path (view->renderer, path);
					}
					path[1].x = x;
					path[1].y = y;
					prev_valid = TRUE;
				}
			} else
				prev_valid = FALSE;
		}
		gog_renderer_pop_style (view->renderer);
	}
}

static GogObject *
gog_xy_view_point (GogView *view, double x, double y)
{
	return FALSE;
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

static GogObjectClass *series_parent_klass;

static void
gog_xy_series_update (GogObject *obj)
{
	double *x_vals = NULL, *y_vals = NULL;
	int x_len = 0, y_len = 0;
	GogXYSeries *series = GOG_XY_SERIES (obj);
	unsigned old_num = series->base.num_elements;

	if (series->base.values[0].data != NULL) {
		x_vals = go_data_vector_get_values (GO_DATA_VECTOR (series->base.values[0].data));
		x_len = go_data_vector_get_len (
			GO_DATA_VECTOR (series->base.values[0].data));
	}
	if (series->base.values[1].data != NULL) {
		y_vals = go_data_vector_get_values (GO_DATA_VECTOR (series->base.values[1].data));
		y_len = go_data_vector_get_len (
			GO_DATA_VECTOR (series->base.values[1].data));
	}
	series->base.num_elements = MIN (x_len, y_len);

	/* queue plot for redraw */
	gog_object_request_update (GOG_OBJECT (series->base.plot));
	if (old_num != series->base.num_elements)
		gog_plot_request_cardinality_update (series->base.plot);

	if (series_parent_klass->update)
		series_parent_klass->update (obj);
}

static void
gog_xy_series_class_init (GObjectClass *gobject_klass)
{
	GogObjectClass *gog_klass = (GogObjectClass *)gobject_klass;

	series_parent_klass = g_type_class_peek_parent (gobject_klass);
	gog_klass->update = gog_xy_series_update;
}

GSF_CLASS (GogXYSeries, gog_xy_series,
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
