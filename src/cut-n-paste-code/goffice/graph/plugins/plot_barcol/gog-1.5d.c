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
#include <goffice/graph/gog-theme.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/go-data.h>
#include <goffice/utils/go-color.h>

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

static void
gog_plot1_5d_update (GogObject *obj)
{
	GogPlot1_5d *model = GOG_PLOT1_5D (obj);
	GogPlot1_5dClass *klass = GOG_PLOT1_5D_GET_CLASS (obj);
	GogSeries1_5d const *series;
	unsigned i, num_elements, num_series;
	double **vals, minimum, maximum;
	unsigned *lengths;
	GSList *ptr;

	model->minimum = model->maximum = 0.;
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
				series->base.values[1].data), &minimum, &maximum);
			if (model->minimum > minimum)
				model->minimum = minimum;
			if (model->maximum < maximum)
				model->maximum = maximum;
		}
	}
	model->num_elements = num_elements;
	model->num_series = num_series;

	if (num_elements <= 0 || num_series <= 0)
		return;

	vals = g_alloca (num_series * sizeof (double *));
	lengths = g_alloca (num_series * sizeof (unsigned));
	i = 0;
	for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next, i++) {
		series = ptr->data;
		if (!gog_series_is_valid (GOG_SERIES (series)))
			continue;
		vals[i] = go_data_vector_get_values (
			GO_DATA_VECTOR (series->base.values[1].data));
		lengths[i] = go_data_vector_get_len (
			GO_DATA_VECTOR (series->base.values[1].data));
	}

	klass->update_stacked_and_percentage (model, vals, lengths);

	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
	if (plot1_5d_parent_klass->update)
		plot1_5d_parent_klass->update (obj);
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
		plot_klass->desc.series.num_dim = G_N_ELEMENTS(dimensions);
	}
	plot_klass->desc.num_series_min = 1;
	plot_klass->desc.num_series_max = G_MAXINT;
	plot_klass->series_type = gog_series1_5d_get_type ();
	plot_klass->axis_set_pref     = gog_plot1_5d_axis_set_pref;
	plot_klass->axis_set_is_valid = gog_plot1_5d_axis_set_is_valid;
	plot_klass->axis_set_assign   = gog_plot1_5d_axis_set_assign;
	plot_klass->supports_vary_style_by_element = gog_1_5d_supports_vary_style_by_element;
}

GSF_CLASS_ABSTRACT (GogPlot1_5d, gog_plot1_5d,
		    gog_plot1_5d_class_init, NULL,
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
