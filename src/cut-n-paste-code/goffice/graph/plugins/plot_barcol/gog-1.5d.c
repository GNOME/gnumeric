/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-1.5d.c
 *
 * Copyright (C) 2003
 *	Jody Goldberg (jody@gnome.org)
 *	Emmanuel Pacaud (emmanuel.pacaud@univ-poitiers.fr)
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
#include "gog-1.5d.h"
#include "gog-line.h"
#include "gog-barcol.h"
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/gog-axis.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/go-data.h>
#include <goffice/utils/go-color.h>
#include <goffice/utils/go-format.h>

#include <module-plugin-defs.h>
#include <src/gnumeric-i18n.h>
#include <src/mathfunc.h>
#include <gsf/gsf-impl-utils.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

enum {
	GOG_1_5D_PROP_0,
	GOG_1_5D_PROP_TYPE
};

static GogObjectClass *plot1_5d_parent_klass;

static void
gog_plot_1_5d_clear_formats (GogPlot1_5d *plot)
{
	if (plot->fmt != NULL) {
		go_format_unref (plot->fmt);
		plot->fmt = NULL;
	}
}

static void
gog_plot1_5d_finalize (GObject *obj)
{
	gog_plot_1_5d_clear_formats (GOG_PLOT1_5D (obj));
	if (G_OBJECT_CLASS (plot1_5d_parent_klass)->finalize)
		G_OBJECT_CLASS (plot1_5d_parent_klass)->finalize (obj);
}

static void
gog_plot1_5d_set_property (GObject *obj, guint param_id,
			    GValue const *value, GParamSpec *pspec)
{
	GogPlot1_5d *gog_1_5d = GOG_PLOT1_5D (obj);

	switch (param_id) {
	case GOG_1_5D_PROP_TYPE: {
		char const *str = g_value_get_string (value);
		if (str == NULL)
			return;
		else if (!g_ascii_strcasecmp (str, "normal"))
			gog_1_5d->type = GOG_1_5D_NORMAL;
		else if (!g_ascii_strcasecmp (str, "stacked"))
			gog_1_5d->type = GOG_1_5D_STACKED;
		else if (!g_ascii_strcasecmp (str, "as_percentage"))
			gog_1_5d->type = GOG_1_5D_AS_PERCENTAGE;
		else
			return;
		break;
	}

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
	gog_object_emit_changed (GOG_OBJECT (obj), TRUE);
}

static void
gog_plot1_5d_get_property (GObject *obj, guint param_id,
			      GValue *value, GParamSpec *pspec)
{
	GogPlot1_5d *gog_1_5d = GOG_PLOT1_5D (obj);

	switch (param_id) {
	case GOG_1_5D_PROP_TYPE:
		switch (gog_1_5d->type) {
		case GOG_1_5D_NORMAL:
			g_value_set_static_string (value, "normal");
			break;
		case GOG_1_5D_STACKED:
			g_value_set_static_string (value, "stacked");
			break;
		case GOG_1_5D_AS_PERCENTAGE:
			g_value_set_static_string (value, "as_percentage");
			break;
		}
		break;
		
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static GogAxis *
gog_plot1_5d_get_value_axis (GogPlot1_5d *model)
{
	GogPlot1_5dClass *klass = GOG_PLOT1_5D_GET_CLASS (model);
	if (klass->swap_x_and_y && (*klass->swap_x_and_y ) (model))
		return model->base.axis [GOG_AXIS_X];
	return model->base.axis [GOG_AXIS_Y];
}

static GogAxis *
gog_plot1_5d_get_index_axis (GogPlot1_5d *model)
{
	GogPlot1_5dClass *klass = GOG_PLOT1_5D_GET_CLASS (model);
	if (klass->swap_x_and_y && (*klass->swap_x_and_y ) (model))
		return model->base.axis [GOG_AXIS_Y];
	return model->base.axis [GOG_AXIS_X];
}

static void
gog_plot1_5d_update (GogObject *obj)
{
	GogPlot1_5d *model = GOG_PLOT1_5D (obj);
	GogPlot1_5dClass *klass = GOG_PLOT1_5D_GET_CLASS (obj);
	GogSeries1_5d const *series;
	unsigned i, num_elements, num_series;
	double **vals, minima, maxima;
	double old_minima, old_maxima;
	unsigned *lengths;
	GSList  *ptr;
	GOData  *index_dim = NULL;
	GogPlot *plot_that_labeled_axis;
	GogAxis *axis;

	old_minima =  model->minima;
	old_maxima =  model->maxima;
	model->minima =  DBL_MAX;
	model->maxima = -DBL_MAX;
	gog_plot_1_5d_clear_formats (model);

	num_elements = num_series = 0;
	for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next) {
		series = ptr->data;
		if (!gog_series_is_valid (GOG_SERIES (series)))
			continue;
		num_series++;

		if (num_elements < series->num_elements)
			num_elements = series->num_elements;
		if (GOG_1_5D_NORMAL == model->type) {
			go_data_vector_get_minmax (GO_DATA_VECTOR (
				series->base.values[1].data), &minima, &maxima);
			if (model->minima > minima)
				model->minima = minima;
			if (model->maxima < maxima)
				model->maxima = maxima;
		}
		if (model->fmt == NULL)
			model->fmt = go_data_preferred_fmt (series->base.values[1].data);
		index_dim = GOG_SERIES (series)->values[0].data;
	}
	axis = gog_plot1_5d_get_index_axis (model);
	if (model->num_elements != num_elements ||
	    model->implicit_index ^ (index_dim == NULL) ||
	    (index_dim != gog_axis_get_labels (axis, &plot_that_labeled_axis) &&
	     GOG_PLOT (model) == plot_that_labeled_axis)) {
		model->num_elements = num_elements;
		model->implicit_index = (index_dim == NULL);
		gog_axis_bound_changed (axis, GOG_OBJECT (model));
	}

	model->num_series = num_series;

	if (num_elements <= 0 || num_series <= 0)
		model->minima = model->maxima = 0.;
	else if (model->type != GOG_1_5D_NORMAL) {
		vals = g_alloca (num_series * sizeof (double *));
		lengths = g_alloca (num_series * sizeof (unsigned));
		i = 0;
		for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next, i++) {
			series = ptr->data;
			/* we are guaranteed that at least 1 series is valid above */
			if (!gog_series_is_valid (GOG_SERIES (series)))
				continue;
			vals[i] = go_data_vector_get_values (
				GO_DATA_VECTOR (series->base.values[1].data));
			lengths[i] = go_data_vector_get_len (
				GO_DATA_VECTOR (series->base.values[1].data));
		}

		klass->update_stacked_and_percentage (model, vals, lengths);
	}

	if (old_minima != model->minima || old_maxima != model->maxima)
		gog_axis_bound_changed (
			gog_plot1_5d_get_value_axis (model), GOG_OBJECT (model));

	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
	if (plot1_5d_parent_klass->update)
		plot1_5d_parent_klass->update (obj);
}

static GOData *
gog_plot1_5d_axis_get_bounds (GogPlot *plot, GogAxisType axis,
			      GogPlotBoundInfo *bounds)
{
	GogPlot1_5d *model = GOG_PLOT1_5D (plot);
	if (axis == gog_axis_get_atype (gog_plot1_5d_get_value_axis (model))) {
		bounds->val.minima = model->minima;
		bounds->val.maxima = model->maxima;
		if (model->type == GOG_1_5D_AS_PERCENTAGE) {
			bounds->logical.minima = -1.;
			bounds->logical.maxima =  1.;
			if (bounds->fmt == NULL) {
				bounds->fmt = go_format_ref (
					go_format_default_percentage ());
			}
		} else if (bounds->fmt == NULL && model->fmt != NULL)
			bounds->fmt = go_format_ref (model->fmt);
		return NULL;
	} else if (axis == gog_axis_get_atype (gog_plot1_5d_get_index_axis (model))) {
		GSList *ptr;

		bounds->val.minima = 0.;
		bounds->val.maxima = model->num_elements;
		bounds->logical.minima = 0.;
		bounds->logical.maxima = gnm_nan;
		bounds->is_discrete    = TRUE;

		for (ptr = plot->series; ptr != NULL ; ptr = ptr->next)
			if (gog_series_is_valid (GOG_SERIES (ptr->data)))
				return GOG_SERIES (ptr->data)->values[0].data;
		return NULL;
	}

	g_warning ("not reached");
	return NULL;
}

static GogAxisSet
gog_plot1_5d_axis_set_pref (GogPlot const *plot)
{
	return GOG_AXIS_SET_XY; /* do some magic later for 3d */
}

static gboolean
gog_plot1_5d_axis_set_is_valid (GogPlot const *plot, GogAxisSet type)
{
	return type == GOG_AXIS_SET_XY; /* do some magic later for 3d */
}

static gboolean
gog_plot1_5d_axis_set_assign (GogPlot *plot, GogAxisSet type)
{
	return type == GOG_AXIS_SET_XY; /* do some magic later for 3d */
}

static gboolean
gog_1_5d_supports_vary_style_by_element (GogPlot const *plot)
{
	GogPlot1_5d *gog_1_5d = GOG_PLOT1_5D (plot);
	return gog_1_5d->type == GOG_1_5D_NORMAL;
}

static void
gog_plot1_5d_class_init (GogPlotClass *plot_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) plot_klass;
	GogObjectClass *gog_klass = (GogObjectClass *) plot_klass;

	plot1_5d_parent_klass = g_type_class_peek_parent (plot_klass);
	gobject_klass->set_property = gog_plot1_5d_set_property;
	gobject_klass->get_property = gog_plot1_5d_get_property;
	gobject_klass->finalize = gog_plot1_5d_finalize;

	g_object_class_install_property (gobject_klass, GOG_1_5D_PROP_TYPE,
		g_param_spec_string ("type", "type",
			"How to group multiple series, normal, stacked, as_percentage",
			"normal", G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));

	gog_klass->update	= gog_plot1_5d_update;

	{
		static GogSeriesDimDesc dimensions[] = {
			{ N_("Labels"), GOG_SERIES_SUGGESTED, TRUE,
			  GOG_DIM_LABEL, GOG_MS_DIM_CATEGORIES },
			{ N_("Values"), GOG_SERIES_REQUIRED, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_VALUES }
		};
		plot_klass->desc.series.dim = dimensions;
		plot_klass->desc.series.num_dim = G_N_ELEMENTS (dimensions);
	}
	plot_klass->desc.num_series_min = 1;
	plot_klass->desc.num_series_max = G_MAXINT;
	plot_klass->series_type = gog_series1_5d_get_type ();
	plot_klass->axis_get_bounds   = gog_plot1_5d_axis_get_bounds;
	plot_klass->axis_set_pref     = gog_plot1_5d_axis_set_pref;
	plot_klass->axis_set_is_valid = gog_plot1_5d_axis_set_is_valid;
	plot_klass->axis_set_assign   = gog_plot1_5d_axis_set_assign;
	plot_klass->supports_vary_style_by_element = gog_1_5d_supports_vary_style_by_element;
}

static void
gog_plot1_5d_init (GogPlot1_5d *plot)
{
	plot->fmt = NULL;
}

GSF_CLASS_ABSTRACT (GogPlot1_5d, gog_plot1_5d,
		    gog_plot1_5d_class_init, gog_plot1_5d_init,
		    GOG_PLOT_TYPE)

/*****************************************************************************/

static GogObjectClass *gog_series1_5d_parent_klass;

static void
gog_series1_5d_update (GogObject *obj)
{
	double *vals;
	int len = 0;
	GogSeries1_5d *series = GOG_SERIES1_5D (obj);
	unsigned old_num = series->num_elements;

	if (series->base.values[1].data != NULL) {
		vals = go_data_vector_get_values (GO_DATA_VECTOR (series->base.values[1].data));
		len = go_data_vector_get_len 
			(GO_DATA_VECTOR (series->base.values[1].data));
	}
	series->num_elements = len;

	/* queue plot for redraw */
	gog_object_request_update (GOG_OBJECT (series->base.plot));
	if (old_num != series->num_elements)
		gog_plot_request_cardinality_update (series->base.plot);

	if (gog_series1_5d_parent_klass->update)
		gog_series1_5d_parent_klass->update (obj);
}

static void
gog_series1_5d_class_init (GogObjectClass *obj_klass)
{
	gog_series1_5d_parent_klass = g_type_class_peek_parent (obj_klass);
	obj_klass->update = gog_series1_5d_update;
}

GSF_CLASS (GogSeries1_5d, gog_series1_5d,
	   gog_series1_5d_class_init, NULL,
	   GOG_SERIES_TYPE)

/* Plugin initialization */

void
plugin_init (void)
{
	gog_plot1_5d_get_type ();
	gog_line_plot_get_type ();
	gog_area_plot_get_type ();
	gog_barcol_plot_get_type ();
}

void
plugin_cleanup (void)
{
}
