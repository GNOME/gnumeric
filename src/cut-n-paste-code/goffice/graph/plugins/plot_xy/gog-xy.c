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
} GogXYPlotClass;

enum {
	PLOT_PROP_0,
};

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static GObjectClass *xy_parent_klass;
static GType gog_xy_view_get_type (void);

static char const *
gog_xy_plot_type_name (G_GNUC_UNUSED GogObject const *item)
{
	return N_("PlotXY");
}

static gboolean
gog_xy_plot_foreach_elem (GogPlot *plot, GogEnumFunc handler, gpointer data)
{
	unsigned i, n;
	GogXYPlot const *model = GOG_XY_PLOT (plot);
	GogSeries const *series = plot->series->data; /* start with the first */
	GogTheme *theme = gog_object_get_theme (GOG_OBJECT (plot));
	GogStyle *style;
	GODataVector *labels;
	char *label;

#if 0
	i = 0;
	n = model->base.cardinality;
	style = gog_style_dup (series->base.style);
	labels = NULL;
	if (series->values[0].data != NULL)
		labels = GO_DATA_VECTOR (series->values[0].data);
	for ( ; i < n ; i++) {
		gog_theme_init_style (theme, style, GOG_OBJECT (series),
			model->base.index_num + i);
		label = (labels != NULL)
			? go_data_vector_get_str (labels, i) : NULL;
		if (label == NULL)
			label = g_strdup_printf ("%d", i);
		(handler) (i, style, label, data);
		g_free (label);
	}
	g_object_unref (style);
#endif

	return TRUE;
}

static void
gog_xy_plot_update (GogObject *obj)
{
	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
}

static GogAxisSet
gog_plot_xy_axis_set_pref (GogPlot const *plot)
{
	return GOG_AXIS_SET_XY; /* do some magic later for 3d */
}

static gboolean
gog_plot_xy_axis_set_is_valid (GogPlot const *plot, GogAxisSet type)
{
	return type == GOG_AXIS_SET_XY; /* do some magic later for 3d */
}

static gboolean
gog_plot_xy_axis_set_assign (GogPlot *plot, GogAxisSet type)
{
	return type == GOG_AXIS_SET_XY; /* do some magic later for 3d */
}

static void
gog_xy_plot_class_init (GogPlotClass *plot_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) plot_klass;
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
	plot_klass->foreach_elem = gog_xy_plot_foreach_elem;
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
