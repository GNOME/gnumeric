/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-line.c
 *
 * Copyright (C) 2003 Emmanuel Pacaud (emmanuel.pacaud@univ-poitiers.fr)
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
#include "gog-line.h"
#include "gog-1.5d.h"
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-axis.h>
#include <goffice/graph/go-data.h>
#include <goffice/utils/go-color.h>

#include <src/gnumeric-i18n.h>
#include <src/mathfunc.h>
#include <gsf/gsf-impl-utils.h>

static GType gog_line_view_get_type (void);

static char const *
gog_line_plot_type_name (G_GNUC_UNUSED GogObject const *item)
{
	/* xgettext : the base for how to name bar/col plot objects
	 * eg The 2nd line plot in a chart will be called
	 * 	PlotLine2
	 */
	return N_("PlotLine");
}

static void
gog_line_update_stacked_and_percentage (GogPlot1_5d *model,
					double **vals, unsigned const *lengths)
{
	unsigned i, j;
	double abs_sum, minimum, maximum, sum, tmp;

	for (i = model->num_elements ; i-- > 0 ; ) {
		abs_sum = sum = 0.;
		minimum =  DBL_MAX;
		maximum = -DBL_MAX;
		for (j = 0 ; j < model->num_series ; j++) {
			if (i >= lengths[j])
				continue;
			tmp = vals[j][i];
			if (!finite (tmp))
				continue;
			sum += tmp;
			abs_sum += fabs (tmp);
			if (minimum > sum)
				minimum = sum;
			if (maximum < sum)
				maximum = sum;
		}
		if ((model->type == GOG_1_5D_AS_PERCENTAGE) &&
		    (gnumeric_sub_epsilon (abs_sum) > 0.)) {
			if (model->minimum > minimum / abs_sum)
				model->minimum = minimum / abs_sum;
			if (model->maximum < maximum / abs_sum)
				model->maximum = maximum / abs_sum;
		} else {
			if (model->minimum > minimum)
				model->minimum = minimum;
			if (model->maximum < maximum)
				model->maximum = maximum;
		}
	}
}

static void
gog_line_plot_class_init (GogPlot1_5dClass *gog_plot_1_5d_klass)
{
	GogObjectClass *gog_klass = (GogObjectClass *) gog_plot_1_5d_klass;
	GogPlotClass *plot_klass = (GogPlotClass *) gog_plot_1_5d_klass;

	gog_klass->type_name	= gog_line_plot_type_name;
	gog_klass->view_type	= gog_line_view_get_type ();

	plot_klass->desc.series.style_fields = GOG_STYLE_LINE | GOG_STYLE_MARKER;

	gog_plot_1_5d_klass->update_stacked_and_percentage =
		gog_line_update_stacked_and_percentage;
}

GSF_CLASS (GogLinePlot, gog_line_plot,
	   gog_line_plot_class_init, NULL /*gog_line_plot_init*/,
	   GOG_PLOT1_5D_TYPE)

/*****************************************************************************/

static char const *
gog_area_plot_type_name (G_GNUC_UNUSED GogObject const *item)
{
	/* xgettext : the base for how to name bar/col plot objects
	 * eg The 2nd line plot in a chart will be called
	 * 	PlotArea2
	 */
	return N_("PlotArea");
}
static void
gog_area_plot_class_init (GogObjectClass *gog_klass)
{
	GogPlotClass *plot_klass = (GogPlotClass *) gog_klass;

	plot_klass->desc.series.style_fields = GOG_STYLE_OUTLINE | GOG_STYLE_FILL;

	gog_klass->type_name	= gog_area_plot_type_name;
}
GSF_CLASS (GogAreaPlot, gog_area_plot,
	   gog_area_plot_class_init, NULL,
	   GOG_LINE_PLOT_TYPE)

/*****************************************************************************/

typedef GogPlotView		GogLineView;
typedef GogPlotViewClass	GogLineViewClass;

static void
gog_line_view_render (GogView *view, GogViewAllocation const *bbox)
{
	GogPlot1_5d const *model = GOG_PLOT1_5D (view->model);
	GogPlot1_5dType const type = model->type;
	GogSeries1_5d const *series;
	unsigned i, j, k;
	unsigned num_elements = model->num_elements;
	unsigned num_series = model->num_series;
	GSList *ptr;

	double **vals;
	GogStyle **styles;
	unsigned *lengths;
	ArtVpath **path;

	double scale_x, scale_y, val_min, val_max;
	double offset_x, offset_y;
	double abs_sum, sum, value;
	gboolean is_null, is_area_plot;

	is_area_plot = GOG_IS_PLOT_AREA (model);

	if (num_elements <= 0 || num_series <= 0)
		return;

	if (!gog_axis_get_bounds (GOG_PLOT (model)->axis[1], &val_min, &val_max))
		return;

	vals    = g_alloca (num_series * sizeof (double *));
	lengths = g_alloca (num_series * sizeof (unsigned));
	styles  = g_alloca (num_series * sizeof (GogStyle *));
	path    = g_alloca (num_series * sizeof (ArtVpath *));
	i = 0;
	for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next) {
		series = ptr->data;

		if (!gog_series_is_valid (GOG_SERIES (series)))
			continue;

		vals[i] = go_data_vector_get_values (
			GO_DATA_VECTOR (series->base.values[1].data));
		lengths[i] = go_data_vector_get_len (
			GO_DATA_VECTOR (series->base.values[1].data));
		styles[i] = GOG_STYLED_OBJECT (series)->style;

		if (!is_area_plot)
			path[i] = g_malloc (sizeof (ArtVpath) * (lengths[i] + 2));
		else if (type == GOG_1_5D_NORMAL)
			path[i] = g_malloc (sizeof (ArtVpath) * (lengths[i] + 5));
		else
			path[i] = g_malloc (sizeof (ArtVpath) * (2 * lengths[i] + 3));

		i++;
	}

	if (is_area_plot) {
		scale_x = view->allocation.w / (num_elements - 1);
		offset_x = view->allocation.x;
	} else {
		scale_x = view->allocation.w / num_elements;
		offset_x = view->allocation.x + scale_x / 2.;
	}
	scale_y = view->allocation.h / (val_min - val_max);
	offset_y = view->allocation.h - scale_y * val_min + view->allocation.y;

	for (j = 1; j <= num_elements; j++) {
		sum = abs_sum = 0.0;

		if (type == GOG_1_5D_AS_PERCENTAGE) {
			for (i = 0; i < num_series; i++)
				if (finite (vals[i][j-1]))
					abs_sum += fabs (vals[i][j-1]);
			is_null = (gnumeric_sub_epsilon (abs_sum) <= 0.);
		} else
			is_null = TRUE;

		for (i = 0; i < num_series; i++) {
			if (j > lengths[i])
				continue;

			value = (vals[i] && finite (vals[i][j-1])) ? vals[i][j-1] : 0.0;
			k = 2 * lengths[i] - j + 1;

			if (is_area_plot && (type != GOG_1_5D_NORMAL)) {
				path[i][k].x = offset_x + scale_x * (j - 1);
				path[i][k].code = ART_LINETO;

				if (type == GOG_1_5D_STACKED)
					path[i][k].y = offset_y + scale_y * sum;
				else
					path[i][k].y = is_null ? offset_y : offset_y + scale_y * sum / abs_sum ;
			}

			path[i][j].x = offset_x + scale_x * (j - 1);
			path[i][j].code = ART_LINETO;

			sum += value;

			switch (type) {
				case GOG_1_5D_NORMAL :
					path[i][j].y = offset_y + scale_y * value;
					break;

				case GOG_1_5D_STACKED :
					path[i][j].y = offset_y + scale_y * sum;
					break;

				case GOG_1_5D_AS_PERCENTAGE :
					path[i][j].y = is_null ? offset_y : offset_y + scale_y * sum  / abs_sum;
					break;
			}

		}
	}

	for (i = 0; i < num_series; i++) {

		if (lengths[i] == 0)
			continue;

		gog_renderer_push_style (view->renderer, styles[i]);

		path[i][0].x = path[i][1].x;
		path[i][0].y = path[i][1].y;
		path[i][0].code = ART_MOVETO;

		if (!is_area_plot) {
			double x, y;
			double min_x = view->allocation.x;
			double max_x = min_x + view->allocation.w;
			double min_y = view->allocation.y;
			double max_y = max_x + view->allocation.h;

			path[i][lengths[i] +1].code = ART_END;

			gog_renderer_draw_path (view->renderer,
				path[i], &view->allocation);
			for (j = 1; j <= lengths[i]; j++) {
				x = path[i][j].x;
				y = path[i][j].y;
				if (min_x <= x && x <= max_x && min_y <= y && y <= max_y)
					gog_renderer_draw_marker (view->renderer, x, y);
			}
		} else {
			switch (type) {
			case GOG_1_5D_NORMAL :
				j = lengths[i] + 1;
				path[i][j].x = path[i][j-1].x;
				path[i][j].y = offset_y;
				path[i][j].code = ART_LINETO;
				j++;
				path[i][j].x = path[i][0].x;
				path[i][j].y = offset_y;
				path[i][j].code = ART_LINETO;
				j++;
				path[i][j].x = path[i][0].x;
				path[i][j].y = path[i][0].y;
				path[i][j].code = ART_LINETO;
				path[i][j+1].code = ART_END;
				break;

			case GOG_1_5D_STACKED :
			case GOG_1_5D_AS_PERCENTAGE :
				j = 2 * lengths[i] + 1;
				path[i][j].x = path[i][0].x;
				path[i][j].y = path[i][0].y;
				path[i][j].code = ART_LINETO;
				path[i][j+1].code = ART_END;
				break;
			}
			gog_renderer_draw_polygon (view->renderer,
				path[i], FALSE,
				/* FIXME FIXME FIXME :
				 * clipping breaks badly here.
				 * for positive areas the winding is backwards and things clip badly */
				 NULL /* &view->allocation */);
		}

		gog_renderer_pop_style (view->renderer);
	}

	for (i = 0; i < num_series; i++)
		if (lengths[i] > 0)
			g_free (path[i]);
}

static void
gog_line_view_class_init (GogViewClass *view_klass)
{
	view_klass->render = gog_line_view_render;
}

static GSF_CLASS (GogLineView, gog_line_view,
		  gog_line_view_class_init, NULL,
		  GOG_PLOT_VIEW_TYPE)
