/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-xy.c
 *
 * Copyright (C) 2003-2004 Jody Goldberg (jody@gnome.org)
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

#include <goffice/goffice-config.h>
#include "gog-xy.h"
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-theme.h>
#include <goffice/graph/gog-axis.h>
#include <goffice/graph/go-data.h>
#include <goffice/graph/gog-error-bar.h>
#include <goffice/utils/go-color.h>
#include <goffice/utils/go-marker.h>
#include <goffice/utils/go-format.h>
#include <goffice/utils/go-math.h>

#include <module-plugin-defs.h>
#include <glib/gi18n.h>
#include <gtk/gtklabel.h>
#include <gsf/gsf-impl-utils.h>
#include <math.h>

typedef struct {
	GogPlotClass	base;
	
	void (*adjust_bounds) (Gog2DPlot *model, double *x_min, double *x_max, double *y_min, double *y_max);
} Gog2DPlotClass;

typedef Gog2DPlotClass GogXYPlotClass;

typedef Gog2DPlotClass GogBubblePlotClass;

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static GogObjectClass *plot2d_parent_klass;
static void gog_2d_plot_adjust_bounds (Gog2DPlot *model, double *x_min, double *x_max, double *y_min, double *y_max);

#define GOG_2D_PLOT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GOG_2D_PLOT_TYPE, Gog2DPlotClass))

static void
gog_2d_plot_clear_formats (Gog2DPlot *plot2d)
{
	if (plot2d->x.fmt != NULL) {
		go_format_unref (plot2d->x.fmt);
		plot2d->x.fmt = NULL;
	}
	if (plot2d->y.fmt != NULL) {
		go_format_unref (plot2d->y.fmt);
		plot2d->y.fmt = NULL;
	}
}

static void
gog_2d_plot_update (GogObject *obj)
{
	Gog2DPlot *model = GOG_2D_PLOT (obj);
	GogXYSeries const *series = NULL;
	double x_min, x_max, y_min, y_max, tmp_min, tmp_max;
	GSList *ptr;
	gboolean is_discrete = FALSE;

	x_min = y_min =  DBL_MAX;
	x_max = y_max = -DBL_MAX;
	gog_2d_plot_clear_formats (model);
	for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next) {
		series = ptr->data;
		if (!gog_series_is_valid (GOG_SERIES (series)))
			continue;

		go_data_vector_get_minmax (GO_DATA_VECTOR (
			series->base.values[1].data), &tmp_min, &tmp_max);
		if (y_min > tmp_min) y_min = tmp_min;
		if (y_max < tmp_max) y_max = tmp_max;
		if (model->y.fmt == NULL)
			model->y.fmt = go_data_preferred_fmt (series->base.values[1].data);

		if (series->base.values[0].data != NULL) {
			go_data_vector_get_minmax (GO_DATA_VECTOR (
				series->base.values[0].data), &tmp_min, &tmp_max);

			if (!go_finite (tmp_min) || !go_finite (tmp_max) ||
			    tmp_min > tmp_max) {
				tmp_min = 0;
				tmp_max = go_data_vector_get_len (
					GO_DATA_VECTOR (series->base.values[1].data));

				is_discrete = TRUE;
			} else if (model->x.fmt == NULL)
				model->x.fmt = go_data_preferred_fmt (series->base.values[0].data);
		} else {
			tmp_min = 0;
			tmp_max = go_data_vector_get_len (
				GO_DATA_VECTOR (series->base.values[1].data));
			is_discrete = TRUE;
		}

		if (x_min > tmp_min) x_min = tmp_min;
		if (x_max < tmp_max) x_max = tmp_max;
	}

	/*adjust bounds to allow large markers or bubbles*/
	gog_2d_plot_adjust_bounds (model, &x_min, &x_max, &y_min, &y_max);
	/* add room for error bars */
	if (gog_error_bar_is_visible (series->x_errors)) {
		gog_error_bar_get_minmax (series->x_errors, &tmp_min, &tmp_max);
		if (x_min > tmp_min)
			x_min = tmp_min;
		if (x_max < tmp_max)
			x_max = tmp_max;
	}
	if (gog_error_bar_is_visible (series->y_errors)) {
		gog_error_bar_get_minmax (series->y_errors, &tmp_min, &tmp_max);
		if (y_min > tmp_min)
			y_min = tmp_min;
		if (y_max < tmp_max)
			y_max = tmp_max;
	}
	
	if (model->x.minima != x_min || model->x.maxima != x_max) {
		model->x.minima = x_min;
		model->x.maxima = x_max;
		gog_axis_bound_changed (model->base.axis[0], GOG_OBJECT (model));
	}
	if (model->y.minima != y_min || model->y.maxima != y_max) {
		model->y.minima = y_min;
		model->y.maxima = y_max;
		gog_axis_bound_changed (model->base.axis[1], GOG_OBJECT (model));
	}
	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
	if (plot2d_parent_klass->update)
		plot2d_parent_klass->update (obj);
}

static void
gog_2d_plot_real_adjust_bounds (Gog2DPlot *model, double *x_min, double *x_max, double *y_min, double *y_max)
{
}

static void
gog_2d_plot_adjust_bounds (Gog2DPlot *model, double *x_min, double *x_max, double *y_min, double *y_max)
{
	Gog2DPlotClass *klass = GOG_2D_PLOT_GET_CLASS (model);
	klass->adjust_bounds (model, x_min, x_max, y_min, y_max);
}

static GogAxisSet
gog_2d_plot_axis_set_pref (GogPlot const *plot)
{
	return GOG_AXIS_SET_XY;
}

static gboolean
gog_2d_plot_axis_set_is_valid (GogPlot const *plot, GogAxisSet type)
{
	return type == GOG_AXIS_SET_XY;
}

static gboolean
gog_2d_plot_axis_set_assign (GogPlot *plot, GogAxisSet type)
{
	return type == GOG_AXIS_SET_XY;
}

static GOData *
gog_2d_plot_axis_get_bounds (GogPlot *plot, GogAxisType axis,
			     GogPlotBoundInfo *bounds)
{
	Gog2DPlot *model = GOG_2D_PLOT (plot);

	if (axis == GOG_AXIS_X) {
		GSList *ptr;

		bounds->val.minima = model->x.minima;
		bounds->val.maxima = model->x.maxima;
		bounds->is_discrete = model->x.minima > model->x.maxima ||
			!go_finite (model->x.minima) ||
			!go_finite (model->x.maxima);
		if (bounds->fmt == NULL && model->x.fmt != NULL)
			bounds->fmt = go_format_ref (model->x.fmt);

		for (ptr = plot->series; ptr != NULL ; ptr = ptr->next)
			if (gog_series_is_valid (GOG_SERIES (ptr->data)))
				return GOG_SERIES (ptr->data)->values[0].data;
		return NULL;
	} 
	
	if (axis == GOG_AXIS_Y) {
		bounds->val.minima = model->y.minima;
		bounds->val.maxima = model->y.maxima;
		if (bounds->fmt == NULL && model->y.fmt != NULL)
			bounds->fmt = go_format_ref (model->y.fmt);
	}
	return NULL;
}

static void
gog_2d_finalize (GObject *obj)
{
	gog_2d_plot_clear_formats (GOG_2D_PLOT (obj));
	G_OBJECT_CLASS (plot2d_parent_klass)->finalize (obj);
}

static GType gog_xy_view_get_type (void);
static GType gog_xy_series_get_type (void);

static void
gog_2d_plot_class_init (GogPlotClass *plot_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) plot_klass;
	GogObjectClass *gog_klass = (GogObjectClass *) plot_klass;
	Gog2DPlotClass *gog_2d_plot_klass = (Gog2DPlotClass*) plot_klass;

	gog_2d_plot_klass->adjust_bounds = gog_2d_plot_real_adjust_bounds;

	plot2d_parent_klass = g_type_class_peek_parent (plot_klass);

	gobject_klass->finalize     = gog_2d_finalize;

	gog_klass->update	= gog_2d_plot_update;
	gog_klass->view_type	= gog_xy_view_get_type ();

	plot_klass->desc.num_series_min = 1;
	plot_klass->desc.num_series_max = G_MAXINT;
	plot_klass->series_type  = gog_xy_series_get_type ();
	plot_klass->axis_set_pref     = gog_2d_plot_axis_set_pref;
	plot_klass->axis_set_is_valid = gog_2d_plot_axis_set_is_valid;
	plot_klass->axis_set_assign   = gog_2d_plot_axis_set_assign;
	plot_klass->axis_get_bounds   = gog_2d_plot_axis_get_bounds;
}

static void
gog_2d_plot_init (Gog2DPlot *plot2d)
{
	plot2d->base.vary_style_by_element = FALSE;
	plot2d->x.fmt = plot2d->y.fmt = NULL;
}

GSF_CLASS (Gog2DPlot, gog_2d_plot,
	   gog_2d_plot_class_init, gog_2d_plot_init,
	   GOG_PLOT_TYPE)

enum {
	GOG_XY_PROP_0,
	GOG_XY_PROP_DEFAULT_STYLE_HAS_MARKERS,
	GOG_XY_PROP_DEFAULT_STYLE_HAS_LINES
};

static GogObjectClass *xy_parent_klass;

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

	gog_klass->type_name	= gog_xy_plot_type_name;

	{
		static GogSeriesDimDesc dimensions[] = {
			{ N_("X"), GOG_SERIES_SUGGESTED, FALSE,
			  GOG_DIM_INDEX, GOG_MS_DIM_CATEGORIES },
			{ N_("Y"), GOG_SERIES_REQUIRED, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_VALUES },
/* Names of the error data are not translated since they are not used */
			{ "Y+err", GOG_SERIES_ERRORS, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_ERR_plus1 },
			{ "Y-err", GOG_SERIES_ERRORS, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_ERR_minus1 },
			{ "X+err", GOG_SERIES_ERRORS, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_ERR_plus2 },
			{ "X-err", GOG_SERIES_ERRORS, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_ERR_minus2 }
		};
		plot_klass->desc.series.dim = dimensions;
		plot_klass->desc.series.num_dim = G_N_ELEMENTS (dimensions);
		plot_klass->desc.series.style_fields = GOG_STYLE_LINE | GOG_STYLE_MARKER;
	}
}

static void
gog_xy_plot_init (GogXYPlot *xy)
{
	xy->default_style_has_markers = TRUE;
	xy->default_style_has_lines = TRUE;
}

GSF_CLASS (GogXYPlot, gog_xy_plot,
	   gog_xy_plot_class_init, gog_xy_plot_init,
	   GOG_2D_PLOT_TYPE)

/*****************************************************************************/

static GogObjectClass *bubble_parent_klass;

#define GOG_BUBBLE_PLOT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GOG_BUBBLE_PLOT_TYPE, GogBubblePlotClass))

static void gog_bubble_plot_adjust_bounds (Gog2DPlot *model, double *x_min, double *x_max, double *y_min, double *y_max);

static char const *
gog_bubble_plot_type_name (G_GNUC_UNUSED GogObject const *item)
{
	return N_("PlotBubble");
}

extern gpointer gog_bubble_plot_pref (GogBubblePlot *bubble, GnmCmdContext *cc);
static gpointer
gog_bubble_plot_editor (GogObject *item, G_GNUC_UNUSED GogDataAllocator *dalloc,
			GnmCmdContext *cc)
{
	return gog_bubble_plot_pref (GOG_BUBBLE_PLOT (item), cc);
}

enum {
	GOG_BUBBLE_PROP_0,
	GOG_BUBBLE_PROP_AS_AREA,
	GOG_BUBBLE_PROP_SHOW_NEGATIVES,
	GOG_BUBBLE_PROP_IN_3D,
	GOG_BUBBLE_PROP_SCALE
};

static void
gog_bubble_plot_set_property (GObject *obj, guint param_id,
			      GValue const *value, GParamSpec *pspec)
{
	GogBubblePlot *bubble = GOG_BUBBLE_PLOT (obj);

	switch (param_id) {
	case GOG_BUBBLE_PROP_AS_AREA :
		bubble->size_as_area = g_value_get_boolean (value);
		break;
	case GOG_BUBBLE_PROP_SHOW_NEGATIVES :
		bubble->show_negatives = g_value_get_boolean (value);
		break;
	case GOG_BUBBLE_PROP_IN_3D :
		bubble->in_3d = g_value_get_boolean (value);
		break;
	case GOG_BUBBLE_PROP_SCALE :
		bubble->bubble_scale = g_value_get_float (value);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}

	/* none of the attributes triggers a size change yet.
	 * When we add data labels we'll need it */
	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
}

static void
gog_bubble_plot_get_property (GObject *obj, guint param_id,
			  GValue *value, GParamSpec *pspec)
{
	GogBubblePlot *bubble = GOG_BUBBLE_PLOT (obj);

	switch (param_id) {
	case GOG_BUBBLE_PROP_AS_AREA :
		g_value_set_boolean (value, bubble->size_as_area);
		break;
	case GOG_BUBBLE_PROP_SHOW_NEGATIVES :
		g_value_set_boolean (value, bubble->show_negatives);
		break;
	case GOG_BUBBLE_PROP_IN_3D :
		g_value_set_boolean (value, bubble->in_3d);
		break;
	case GOG_BUBBLE_PROP_SCALE :
		g_value_set_float (value, bubble->bubble_scale);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gog_bubble_plot_class_init (GogPlotClass *plot_klass)
{
	GogObjectClass *gog_klass = (GogObjectClass *) plot_klass;
	GObjectClass *gobject_klass = (GObjectClass *) plot_klass;
	Gog2DPlotClass *gog_2d_plot_klass = (Gog2DPlotClass*) plot_klass;

	bubble_parent_klass = g_type_class_peek_parent (plot_klass);
	gobject_klass->set_property = gog_bubble_plot_set_property;
	gobject_klass->get_property = gog_bubble_plot_get_property;

	gog_2d_plot_klass->adjust_bounds = gog_bubble_plot_adjust_bounds;
	gog_klass->type_name	= gog_bubble_plot_type_name;

	gog_klass->editor	= gog_bubble_plot_editor;

	g_object_class_install_property (gobject_klass, GOG_BUBBLE_PROP_AS_AREA,
		g_param_spec_boolean ("size_as_area", "size_as_area",
			"Display size as area instead of diameter",
			TRUE,
			G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, GOG_BUBBLE_PROP_SHOW_NEGATIVES,
		g_param_spec_boolean ("show_negatives", "show_negatives",
			"Draw bubbles for negative values",
			FALSE,
			G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, GOG_BUBBLE_PROP_IN_3D,
		g_param_spec_boolean ("in_3d", "in_3d",
			"Draw 3d bubbles",
			FALSE,
			G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, GOG_BUBBLE_PROP_SCALE,
		g_param_spec_float ("bubble_scale", "bubble_scale",
			"Fraction of default radius used for display.",
			0., 2., 1.,
			G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	{
		static GogSeriesDimDesc dimensions[] = {
			{ N_("X"), GOG_SERIES_SUGGESTED, FALSE,
			  GOG_DIM_INDEX, GOG_MS_DIM_CATEGORIES },
			{ N_("Y"), GOG_SERIES_REQUIRED, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_VALUES },
			{ N_("Bubble"), GOG_SERIES_REQUIRED, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_BUBBLES },
/* Names of the error data are not translated since they are not used */
			{ "Y+err", GOG_SERIES_ERRORS, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_ERR_plus1 },
			{ "Y-err", GOG_SERIES_ERRORS, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_ERR_minus1 },
			{ "X+err", GOG_SERIES_ERRORS, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_ERR_plus2 },
			{ "X-err", GOG_SERIES_ERRORS, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_ERR_minus2 }
		};
		plot_klass->desc.series.dim = dimensions;
		plot_klass->desc.series.num_dim = G_N_ELEMENTS (dimensions);
		plot_klass->desc.series.style_fields = GOG_STYLE_OUTLINE | GOG_STYLE_FILL;
	}
}

#define BUBBLE_MAX_RADIUS_RATIO 8.
static void
gog_bubble_plot_adjust_bounds (Gog2DPlot *model, double *x_min, double *x_max, double *y_min, double *y_max)
{
	/* Add room for bubbles*/
	double tmp;
	double factor = BUBBLE_MAX_RADIUS_RATIO / GOG_BUBBLE_PLOT (model)->bubble_scale - 2.;
	tmp = (*x_max - *x_min) / factor;
	*x_min -= tmp;
	*x_max += tmp;
	tmp = (*y_max - *y_min) / factor;
	*y_min -= tmp;
	*y_max += tmp;
}

static void
gog_bubble_plot_init (GogBubblePlot *bubble)
{
	bubble->size_as_area = TRUE;
	bubble->in_3d = FALSE;
	bubble->show_negatives = FALSE;
	bubble->bubble_scale = 1.0;
}

GSF_CLASS (GogBubblePlot, gog_bubble_plot,
	   gog_bubble_plot_class_init, gog_bubble_plot_init,
	   GOG_2D_PLOT_TYPE)

/*****************************************************************************/
typedef GogPlotView		GogXYView;
typedef GogPlotViewClass	GogXYViewClass;

#define MAX_ARC_SEGMENTS 64

static void
bubble_draw_circle (GogView *view, double x, double y, double radius)
{
	double theta, dt = 2 * M_PI / MAX_ARC_SEGMENTS;
	int i;
	ArtVpath path[MAX_ARC_SEGMENTS + 2];
	path[0].x = path[MAX_ARC_SEGMENTS].x = x + radius;
	path[0].y = path[MAX_ARC_SEGMENTS].y = y;
	path[0].code = ART_MOVETO;
#warning what about small bubbles. With a very small radius, libart emits lot of warnings.
	if (radius < 1.) radius = 1.;
	for (i = 1, theta = dt; i < MAX_ARC_SEGMENTS; i++, theta += dt) {
		path[i].x = x + radius * cos (theta);
		/* must turn clockwise for gradients */
		path[i].y = y - radius * sin (theta);
		path[i].code = ART_LINETO;
	}
	path[MAX_ARC_SEGMENTS].code = ART_LINETO;
	path[MAX_ARC_SEGMENTS + 1].code = ART_END;
	gog_renderer_draw_polygon (view->renderer, path, FALSE, &view->residual);
}

typedef struct {
	double x, y;
} MarkerData;

static void
gog_xy_view_render (GogView *view, GogViewAllocation const *bbox)
{
	Gog2DPlot const *model = GOG_2D_PLOT (view->model);
	unsigned num_series;
	GogAxisMap *x_map, *y_map;
	GogXYSeries const *series = NULL;
	unsigned i ,j ,k ,n, tmp;
	GogTheme *theme = gog_object_get_theme (GOG_OBJECT (model));
	GogStyle *neg_style = NULL;
	GSList *ptr;
	double const *y_vals, *x_vals = NULL, *z_vals = NULL;
	double x = 0., y = 0., z, x_canvas = 0., y_canvas = 0.;
	double zmax, rmax = 0.;
	double x_margin_min, x_margin_max, y_margin_min, y_margin_max, margin;
	double xerrmin, xerrmax, yerrmin, yerrmax;
	double prev_x = 0., prev_y = 0.; 
	double prev_x_canvas = 0., prev_y_canvas = 0.;
	ArtVpath	path[3];
	GogStyle *style = NULL;
	gboolean valid, prev_valid, show_marks, show_lines, show_negatives, in_3d, size_as_area = TRUE;

	MarkerData **markers;
	unsigned *num_markers;

	x_map = gog_axis_map_new (model->base.axis[0], 
				  view->residual.x , view->residual.w);
	y_map = gog_axis_map_new (model->base.axis[1], 
				  view->residual.y + view->residual.h, 
				  -view->residual.h);

	if (!(gog_axis_map_is_valid (x_map) &&
	      gog_axis_map_is_valid (y_map))) {
		gog_axis_map_free (x_map);
		gog_axis_map_free (y_map);
		return;
	}

	gog_renderer_clip_push (view->renderer, &view->allocation);

	for (num_series = 0, ptr = model->base.series ; ptr != NULL ; ptr = ptr->next, num_series++);
	markers = g_alloca (num_series * sizeof (MarkerData *));
	num_markers = g_alloca (num_series * sizeof (unsigned));

	path[0].code = ART_MOVETO;
	path[1].code = ART_LINETO;
	path[2].code = ART_END;
	for (j = 0, ptr = model->base.series ; ptr != NULL ; ptr = ptr->next, j++) {
		series = ptr->data;
		markers[j] = NULL;

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
		style = GOG_STYLED_OBJECT (series)->style;
		if (GOG_IS_BUBBLE_PLOT (model)) {
			double zmin;
			go_data_vector_get_minmax (GO_DATA_VECTOR (series->base.values[2].data), &zmin, &zmax);
			show_negatives = GOG_BUBBLE_PLOT (view->model)->show_negatives;
			if ((! go_finite (zmax)) || (!show_negatives && (zmax <= 0))) continue;
			rmax = MIN (view->residual.w, view->residual.h) / BUBBLE_MAX_RADIUS_RATIO
						* GOG_BUBBLE_PLOT (view->model)->bubble_scale;
			size_as_area = GOG_BUBBLE_PLOT (view->model)->size_as_area;
			in_3d = GOG_BUBBLE_PLOT (view->model)->in_3d;
			if (show_negatives) {
				zmin = fabs (zmin);
				if (zmin > zmax) zmax = zmin;
				neg_style = gog_style_dup (GOG_STYLED_OBJECT (series)->style);
				neg_style->fill.type = GOG_FILL_STYLE_PATTERN;
				neg_style->fill.pattern.pattern = GO_PATTERN_SOLID;
				neg_style->fill.pattern.back = RGBA_WHITE;
			}
			if (model->base.vary_style_by_element)
				style = gog_style_dup (style);
	
			z_vals = go_data_vector_get_values (
				GO_DATA_VECTOR (series->base.values[2].data));
			tmp = go_data_vector_get_len (
				GO_DATA_VECTOR (series->base.values[2].data));
			if (n > tmp)
				n = tmp;
		}

		if (n <= 0)
			continue;

		show_marks = gog_style_is_marker_visible (style);
		show_lines = gog_style_is_line_visible (style);
		if (!show_marks && !show_lines)
			continue;

		if (show_marks && !GOG_IS_BUBBLE_PLOT (model))
			markers[j] = g_new (MarkerData, n);

		prev_valid = FALSE;
		gog_renderer_push_style (view->renderer, style);
		margin = gog_renderer_line_size (view->renderer, 1);
		x_margin_min = view->allocation.x - margin;
		x_margin_max = view->allocation.x + view->allocation.w + margin;
		y_margin_min = view->allocation.y - margin;
		y_margin_max = view->allocation.y + view->allocation.h + margin;

		k = 0;
		for (i = 1 ; i <= n ; i++) {
			x = x_vals ? *x_vals++ : i;
			y = *y_vals++;
			valid = !isnan (y) && !isnan (x);
			if (valid) {
				/* We are checking with go_finite here because isinf
				   if not available everywhere.  Note, that NANs
				   have been ruled out.  */
				if (!go_finite (y))
					y = 0; /* excel is just sooooo consistent */
				if (!go_finite (x))
					x = i;
				x_canvas = gog_axis_map_to_canvas (x_map, x);
				y_canvas = gog_axis_map_to_canvas (y_map, y);
				if (GOG_IS_BUBBLE_PLOT(model)) {
					z = *z_vals++;
					if (!go_finite (z)) continue;
					if (z < 0) {
						if (GOG_BUBBLE_PLOT(model)->show_negatives) {
							gog_renderer_push_style (view->renderer, neg_style);
							bubble_draw_circle (view, x_canvas, y_canvas, 
									    ((size_as_area)? sqrt (- z / zmax): - z / zmax) * rmax);
							gog_renderer_pop_style (view->renderer);
						} else continue;
					} else {
						if (model->base.vary_style_by_element)
							gog_theme_fillin_style (theme, style, GOG_OBJECT (series),
								model->base.index_num + i - 1, FALSE);
						bubble_draw_circle (view, x_canvas, y_canvas, ((size_as_area)? sqrt (z / zmax): z / zmax) * rmax);
					}
				} else	if (prev_valid && show_lines) {
					path[0].x = prev_x_canvas;
					path[0].y = prev_y_canvas;
					path[1].x = x_canvas;
					path[1].y = y_canvas;
					gog_renderer_draw_path (view->renderer, path, NULL);
				}
			}

			/* draw error bars after line */
			if (prev_valid) {
				if (gog_error_bar_is_visible (series->x_errors)) {
						GogErrorBar const *bar = series->x_errors;
					 if (gog_error_bar_get_bounds (bar, i - 2, &xerrmin, &xerrmax)) {
						 gog_error_bar_render (bar, view->renderer, 
								       x_map, y_map, 
								       prev_x, prev_y, 
								       xerrmin, xerrmax, TRUE);
					 }
				}
				if (gog_error_bar_is_visible (series->y_errors)) {
					GogErrorBar const *bar = series->y_errors;
					 if (gog_error_bar_get_bounds (bar, i - 2, &yerrmin, &yerrmax)) {
						 gog_error_bar_render (bar, view->renderer, 
								       x_map, y_map, prev_x, prev_y, 
								       yerrmin, yerrmax, FALSE);
					 }
				}
			}

			/* draw marker after line */
			if (prev_valid && show_marks &&
			    x_margin_min <= prev_x_canvas && prev_x_canvas <= x_margin_max &&
			    y_margin_min <= prev_y_canvas && prev_y_canvas <= y_margin_max) {
				markers[j][k].x = prev_x_canvas;
				markers[j][k].y = prev_y_canvas;
				k++;
			}

			prev_x_canvas = x_canvas;
			prev_y_canvas = y_canvas;
			prev_x = x;
			prev_y = y;
			prev_valid = valid;
		}

		/* draw error bars after line */
		if (gog_error_bar_is_visible (series->x_errors)) {
			GogErrorBar const *bar = series->x_errors;
			if (gog_error_bar_get_bounds (bar, i - 2, &xerrmin, &xerrmax)) {
				gog_error_bar_render (bar, view->renderer, 
						      x_map, y_map, prev_x, prev_y, 
						      xerrmin, xerrmax, TRUE);
			}
		}
		if (gog_error_bar_is_visible (series->y_errors)) {
			GogErrorBar const *bar = series->y_errors;
			if (gog_error_bar_get_bounds (bar, i - 2, &yerrmin, &yerrmax)) {
				gog_error_bar_render (bar, view->renderer, 
						      x_map, y_map, prev_x, prev_y, 
						      yerrmin, yerrmax, FALSE);
			}
		}

		/* draw marker after line */
		if (prev_valid && show_marks &&
		    x_margin_min <= prev_x_canvas && prev_x_canvas <= x_margin_max &&
		    y_margin_min <= prev_y_canvas && prev_y_canvas <= y_margin_max) {
			markers[j][k].x = x_canvas;
			markers[j][k].y = y_canvas;
			k++;
		}

		gog_renderer_pop_style (view->renderer);
		num_markers[j] = k;
	}

	if (GOG_IS_BUBBLE_PLOT (model)) {
		if (model->base.vary_style_by_element)
			g_object_unref (style);
		if (((GogBubblePlot*)model)->show_negatives)
			g_object_unref (neg_style);
	}

	gog_renderer_clip_pop (view->renderer);

	if (!GOG_IS_BUBBLE_PLOT (model))
		for (j = 0, ptr = model->base.series ; ptr != NULL ; ptr = ptr->next, j++) {
				if (markers[j] != NULL) {
					series = ptr->data;
					style = GOG_STYLED_OBJECT (series)->style;
					gog_renderer_push_style (view->renderer, style);
					for (k = 0; k < num_markers[j]; k++)
						gog_renderer_draw_marker (view->renderer, 
									  markers[j][k].x,
									  markers[j][k].y);
					gog_renderer_pop_style (view->renderer);
					g_free (markers[j]);
				}
			}

	gog_axis_map_free (x_map);
	gog_axis_map_free (y_map);
}

static gboolean
gog_xy_view_info_at_point (GogView *view, double x, double y,
			   GogObject const *cur_selection,
			   GogObject **obj, char **name)
{
	return FALSE;
}

static void
gog_xy_view_class_init (GogViewClass *view_klass)
{
	view_klass->render	  = gog_xy_view_render;
	view_klass->info_at_point = gog_xy_view_info_at_point;
	view_klass->clip	  = FALSE;
}

static GSF_CLASS (GogXYView, gog_xy_view,
		  gog_xy_view_class_init, NULL,
		  GOG_PLOT_VIEW_TYPE)

/*****************************************************************************/

typedef GogSeriesClass GogXYSeriesClass;

enum {
	SERIES_PROP_0,
	SERIES_PROP_XERRORS,
	SERIES_PROP_YERRORS
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
	if (GOG_IS_BUBBLE_PLOT (series->base.plot)) {
		double *z_vals = NULL;
		int z_len = 0;
		if (series->base.values[2].data != NULL) {
			z_vals = go_data_vector_get_values (GO_DATA_VECTOR (series->base.values[2].data));
			z_len = go_data_vector_get_len (
				GO_DATA_VECTOR (series->base.values[2].data));
			if (y_len > z_len) y_len = z_len;
		}
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
gog_xy_series_init (GObject *obj)
{
	GogXYSeries *series = GOG_XY_SERIES (obj);

	series->x_errors = series->y_errors = NULL;
}

static void
gog_xy_series_finalize (GObject *obj)
{
	GogXYSeries *series = GOG_XY_SERIES (obj);

	if (series->x_errors != NULL) {
		g_object_unref (series->x_errors); 
		series->x_errors = NULL;
	}

	if (series->y_errors != NULL) {
		g_object_unref (series->y_errors); 
		series->y_errors = NULL;
	}

	G_OBJECT_CLASS (series_parent_klass)->finalize (obj);
}

static void
gog_xy_series_init_style (GogStyledObject *gso, GogStyle *style)
{
	GogSeries *series = GOG_SERIES (gso);
	GogXYPlot const *plot;

	series_parent_klass->init_style (gso, style);
	if (series->plot == NULL ||
	    GOG_IS_BUBBLE_PLOT (series->plot))
		return;

	plot = GOG_XY_PLOT (series->plot);
	if (!plot->default_style_has_markers) {
		style->disable_theming |= GOG_STYLE_MARKER;
		if (style->marker.auto_shape) {
			GOMarker *m = go_marker_new ();
			go_marker_set_shape (m, GO_MARKER_NONE);
			gog_style_set_marker (style, m);
		}
	}
	if (!plot->default_style_has_lines) {
		style->disable_theming |= GOG_STYLE_LINE;
		if (style->line.auto_dash)
			style->line.dash_type = GO_LINE_NONE;
		if (style->line.auto_color) {
			style->line.width = 0;
			style->line.color = RGBA_BLACK;
		}
	}
}

static void
gog_xy_series_set_property (GObject *obj, guint param_id,
			    GValue const *value, GParamSpec *pspec)
{
	GogXYSeries *series=  GOG_XY_SERIES (obj);
	GogErrorBar* bar;

	switch (param_id) {
	case SERIES_PROP_XERRORS :
		bar = g_value_get_object (value);
		if (series->x_errors == bar)
			return;
		if (bar) {
			bar = gog_error_bar_dup (bar);
			bar->series = GOG_SERIES (series);
			bar->dim_i = 0;
			bar->error_i = series->base.plot->desc.series.num_dim - 2;
		}
		if (!series->base.needs_recalc) {
			series->base.needs_recalc = TRUE;
			gog_object_emit_changed (GOG_OBJECT (series), FALSE);
		}
		if (series->x_errors != NULL)
			g_object_unref (series->x_errors);
		series->x_errors = bar;
		break;
	case SERIES_PROP_YERRORS :
		bar = g_value_get_object (value);
		if (series->y_errors == bar)
			return;
		if (bar) {
			bar = gog_error_bar_dup (bar);
			bar->series = GOG_SERIES (series);
			bar->dim_i = 1;
			bar->error_i = series->base.plot->desc.series.num_dim - 4;
		}
		if (!series->base.needs_recalc) {
			series->base.needs_recalc = TRUE;
			gog_object_emit_changed (GOG_OBJECT (series), FALSE);
		}
		if (series->y_errors != NULL)
			g_object_unref (series->y_errors);
		series->y_errors = bar;
		break;
	}
}

static void
gog_xy_series_get_property (GObject *obj, guint param_id,
			  GValue *value, GParamSpec *pspec)
{
	GogXYSeries *series=  GOG_XY_SERIES (obj);

	switch (param_id) {
	case SERIES_PROP_XERRORS :
		g_value_set_object (value, series->x_errors);
		break;
	case SERIES_PROP_YERRORS :
		g_value_set_object (value, series->y_errors);
		break;
	}
}

static void 
gog_xy_series_populate_editor (GogSeries *series,
				GtkNotebook *book,
				GogDataAllocator *dalloc,
				GnmCmdContext *cc)
{
	GtkWidget *error_page;
	error_page = gog_error_bar_prefs (series, "y-errors", FALSE, dalloc, cc);
	gtk_notebook_prepend_page (book, error_page, gtk_label_new (_("Y Error bars")));
	error_page = gog_error_bar_prefs (series,"x-errors", TRUE, dalloc, cc);
	gtk_notebook_prepend_page (book, error_page, gtk_label_new (_("X Error bars")));
}

static void
gog_xy_series_class_init (GogStyledObjectClass *gso_klass)
{
	GogObjectClass *gog_klass = (GogObjectClass *)gso_klass;
	GObjectClass *gobject_klass = (GObjectClass *) gso_klass;
	GogSeriesClass *gog_series_klass = (GogSeriesClass*) gso_klass;

	series_parent_klass = g_type_class_peek_parent (gso_klass);
	gog_klass->update	= gog_xy_series_update;
	gso_klass->init_style	= gog_xy_series_init_style;
	gobject_klass->finalize		= gog_xy_series_finalize;
	gobject_klass->set_property = gog_xy_series_set_property;
	gobject_klass->get_property = gog_xy_series_get_property;
	gog_series_klass->populate_editor = gog_xy_series_populate_editor;

	g_object_class_install_property (gobject_klass, SERIES_PROP_XERRORS,
		g_param_spec_object ("x-errors", "x-errors",
			"GogErrorBar *",
			GOG_ERROR_BAR_TYPE, G_PARAM_READWRITE|GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, SERIES_PROP_YERRORS,
		g_param_spec_object ("y-errors", "y-errors",
			"GogErrorBar *",
			GOG_ERROR_BAR_TYPE, G_PARAM_READWRITE|GOG_PARAM_PERSISTENT));
}

static GSF_CLASS (GogXYSeries, gog_xy_series,
	   gog_xy_series_class_init, gog_xy_series_init,
	   GOG_SERIES_TYPE)

void
plugin_init (void)
{
	gog_xy_plot_get_type ();
	gog_bubble_plot_get_type ();
}

void
plugin_cleanup (void)
{
}
