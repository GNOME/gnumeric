/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-plot.c :
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
#include <goffice/graph/gog-plot-impl.h>
#include <goffice/graph/gog-plot-engine.h>
#include <goffice/graph/gog-series-impl.h>
#include <goffice/graph/gog-chart.h>
#include <goffice/graph/gog-axis.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-graph.h>
#include <goffice/graph/go-data.h>
#include <gnumeric-i18n.h>

#include <gsf/gsf-impl-utils.h>
#include <glade/glade-build.h>	/* for the xml utils */
#include <string.h>
#include <stdlib.h>

#define GOG_PLOT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GOG_PLOT_TYPE, GogPlotClass))

static GObjectClass *parent_klass;

static void
gog_plot_finalize (GObject *obj)
{
	GogPlot *plot = GOG_PLOT (obj);

	g_slist_free (plot->series); /* graphitem does the unref */

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static gboolean
role_series_can_add (GogObject const *parent)
{
	GogPlot *plot = GOG_PLOT (parent);
	return g_slist_length (plot->series) < plot->desc.num_series_max;
}
static gboolean
role_series_can_remove (GogObject const *child)
{
	GogPlot const *plot = GOG_PLOT (child->parent);
	return g_slist_length (plot->series) > plot->desc.num_series_min;
}

static GogObject *
role_series_allocate (GogObject *plot)
{
	GogPlotClass *klass = GOG_PLOT_GET_CLASS (plot);
	GType type = klass->series_type;

	if (type == 0)
		type = GOG_SERIES_TYPE;
	return g_object_new (type, NULL);
}

static void
role_series_post_add (GogObject *parent, GogObject *child)
{
	GogPlot *plot = GOG_PLOT (parent);
	GogSeries *series = GOG_SERIES (child);
	unsigned num_dim;
	num_dim = plot->desc.series.num_dim;

	/* Alias things so that dim -1 is valid */
	series->values = g_new0 (GogDim, num_dim+1) + 1;
	series->plot = plot;

	/* if there are other series associated with the plot, and there are 
	 * shared dimensions, clone them over.  */
	if (series->plot->series != NULL) {
		GogGraph *graph = gog_object_get_graph (GOG_OBJECT (plot));
		GogSeriesDesc const *desc = &plot->desc.series;
		GogSeries const *src = plot->series->data;
		unsigned i;

		for (i = num_dim; i-- > 0 ; ) /* name is never shared */
			if (desc->dim[i].is_shared)
				gog_series_set_dim_internal (series, i, src->values[i].data, graph);

		gog_series_check_validity (series);
	}

	/* APPEND to keep order, there won't be that many */
	plot->series = g_slist_append (plot->series, series);
	gog_plot_request_cardinality_update (plot);
}

static void
role_series_pre_remove (GogObject *parent, GogObject *series)
{
	GogPlot *plot = GOG_PLOT (parent);
	plot->series = g_slist_remove (plot->series, series);
	gog_plot_request_cardinality_update (plot);
}

static void
gog_plot_class_init (GogObjectClass *gog_klass)
{
	static GogObjectRole const roles[] = {
		{ N_("Series"), "GogSeries",
		  GOG_POSITION_SPECIAL, GOG_POSITION_SPECIAL, FALSE,
		  role_series_can_add, role_series_can_remove,
		  role_series_allocate,
		  role_series_post_add, role_series_pre_remove, NULL },
	};
	GObjectClass *gobject_klass = (GObjectClass *) gog_klass;

	gog_object_register_roles (gog_klass, roles, G_N_ELEMENTS (roles));
	parent_klass = g_type_class_peek_parent (gog_klass);
	gobject_klass->finalize		= gog_plot_finalize;
}

static void
gog_plot_init (GogPlot *plot)
{
	/* start as true so that we can queue an update when it changes */
	plot->cardinality_valid = TRUE;
}

GSF_CLASS_ABSTRACT (GogPlot, gog_plot,
		    gog_plot_class_init, gog_plot_init,
		    GOG_OBJECT_TYPE)

static void
cb_plot_set_args (char const *name, char const *val, GObject *plot)
{
	GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (plot), name);
	GType prop_type;
	GValue res = { 0 };
	gboolean success = TRUE;

	if (pspec == NULL) {
		g_warning ("unknown property `%s' for class `%s'",
			   name, G_OBJECT_TYPE_NAME (plot));
		return;
	}

	prop_type = G_PARAM_SPEC_VALUE_TYPE (pspec);
	if (val == NULL &&
	    G_TYPE_FUNDAMENTAL (prop_type) != G_TYPE_BOOLEAN) {
		g_warning ("could not convert NULL to type `%s' for property `%s'",
			   g_type_name (prop_type), pspec->name);
		return;
	}

	g_value_init (&res, prop_type);
	switch (G_TYPE_FUNDAMENTAL (prop_type)) {
	case G_TYPE_CHAR:
		g_value_set_char (&res, val[0]);
		break;
	case G_TYPE_UCHAR:
		g_value_set_uchar (&res, (guchar)val[0]);
		break;
	case G_TYPE_BOOLEAN:
		g_value_set_boolean (&res, 
			val == NULL ||
			g_ascii_tolower (val[0]) == 't' ||
			g_ascii_tolower (val[0]) == 'y' ||
			strtol (val, NULL, 0));
		break;
	case G_TYPE_INT:
		g_value_set_int (&res, strtol (val, NULL, 0));
		break;
	case G_TYPE_UINT:
		g_value_set_uint (&res, strtoul (val, NULL, 0));
		break;
	case G_TYPE_LONG:
		g_value_set_long (&res, strtol (val, NULL, 0));
		break;
	case G_TYPE_ULONG:
		g_value_set_ulong (&res, strtoul (val, NULL, 0));
		break; 
	case G_TYPE_ENUM:
		g_value_set_enum (&res, glade_enum_from_string (prop_type, val));
		break;
	case G_TYPE_FLAGS:
		g_value_set_flags (&res, glade_flags_from_string (prop_type, val));
		break;
	case G_TYPE_FLOAT:
		g_value_set_float (&res, g_strtod (val, NULL));
		break;
	case G_TYPE_DOUBLE:
		g_value_set_double (&res, g_strtod (val, NULL));
		break;
	case G_TYPE_STRING:
		g_value_set_string (&res, val);
		break;
#if 0
	/* get GOColor registered */
	case G_TYPE_BOXED:
		if (G_VALUE_HOLDS (&res, GDK_TYPE_COLOR)) {
			GdkColor colour = { 0, };

			if (gdk_color_parse (val, &colour) &&
			    gdk_colormap_alloc_color (gtk_widget_get_default_colormap (),
						      &colour, FALSE, TRUE)) {
				g_value_set_boxed (&res, &colour);
			} else {
				g_warning ("could not parse colour name `%s'", val);
				success = FALSE;
			}
		} else
			success = FALSE;
#endif

	default:
		success = FALSE;
	}

	if (!success) {
		g_warning ("could not convert string to type `%s' for property `%s'",
			   g_type_name (prop_type), pspec->name);
		g_value_unset (&res);
	}
	g_object_set_property (G_OBJECT (plot), name, &res);
}

GogPlot *
gog_plot_new_by_type (GogPlotType const *type)
{
	GogPlot *res;

	g_return_val_if_fail (type != NULL, NULL);

	res = gog_plot_new_by_name (type->engine);
	if (res != NULL && type->properties != NULL)
		g_hash_table_foreach (type->properties,
				      (GHFunc) cb_plot_set_args, res);
	return res;
}

/**
 * gog_plot_make_similar :
 * @dst :
 * @src :
 *
 * As much as possible have @dst use similar formatting and data allocation to
 * @src.
 *
 * return TRUE on failue
 **/
gboolean
gog_plot_make_similar (GogPlot *dst, GogPlot const *src)
{
	g_return_val_if_fail (GOG_PLOT (dst) != NULL, TRUE);
	g_return_val_if_fail (GOG_PLOT (src) != NULL, TRUE);

	return FALSE;
}

 /* convenience routines */
GogSeries *
gog_plot_new_series (GogPlot *plot)
{
	GogObject *res = gog_object_add_by_name (GOG_OBJECT (plot), "Series", NULL);
	return res ? GOG_SERIES (res) : NULL;
}
GogPlotDesc const *
gog_plot_description (GogPlot const *plot)
{
	g_return_val_if_fail (GOG_PLOT (plot) != NULL, NULL);
	return &plot->desc;
}
GogChart *
gog_plot_get_chart (GogPlot const *plot)
{
	return GOG_CHART (GOG_OBJECT (plot)->parent);
}

/* protected */
void
gog_plot_request_cardinality_update (GogPlot *plot)
{
	g_return_if_fail (GOG_PLOT (plot) != NULL);

	if (plot->cardinality_valid) {
		GogChart *chart = gog_plot_get_chart (plot);
		plot->cardinality_valid = FALSE;
		if (chart != NULL)
			gog_chart_request_cardinality_update (chart);
	}
}

unsigned
gog_plot_get_cardinality (GogPlot *plot)
{
	g_return_val_if_fail (GOG_PLOT (plot) != NULL, 0);

	if (!plot->cardinality_valid) {
		GogPlotClass *klass = GOG_PLOT_GET_CLASS (plot);

		plot->cardinality_valid = TRUE;
		plot->index_num = gog_chart_get_cardinality (
			gog_plot_get_chart (plot));
		if (klass->cardinality == NULL) {
			unsigned i = plot->index_num;
			GSList *ptr;
			for (ptr = plot->series; ptr != NULL ; ptr = ptr->next)
				gog_series_set_index (ptr->data, i++, FALSE);
			plot->cardinality = g_slist_length (plot->series);
		} else
			plot->cardinality = (klass->cardinality) (plot);
	}
	return plot->cardinality;
}

void
gog_plot_foreach_elem (GogPlot *plot, GogEnumFunc func, gpointer data)
{
	GogPlotClass *klass = GOG_PLOT_GET_CLASS (plot);

	g_return_if_fail (klass != NULL);
	g_return_if_fail (plot->cardinality_valid);

	if (klass->foreach_elem == NULL ||
	    !(klass->foreach_elem) (plot, func, data)) {
		unsigned i = plot->index_num;
		GSList *ptr;
		for (ptr = plot->series; ptr != NULL ; ptr = ptr->next)
			func (i++, gog_styled_object_get_style (ptr->data),
			      gog_object_get_name (ptr->data), data);

		g_return_if_fail (i == plot->index_num + plot->cardinality);
	}
}
