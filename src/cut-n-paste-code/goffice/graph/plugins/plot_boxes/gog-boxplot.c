/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-boxplot.c
 *
 * Copyright (C) 2005 Jean Brefort (jean.brefort@normalesup.org)
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
#include "gog-boxplot.h"
#include <goffice/graph/gog-series-impl.h>
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-axis.h>
#include <goffice/graph/go-data.h>

#include <src/gui-util.h>

#include <module-plugin-defs.h>
#include <glib/gi18n.h>
#include <glade/glade-xml.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtkenums.h>
#include <gsf/gsf-impl-utils.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static void
cb_gap_changed (GtkAdjustment *adj, GObject *boxplot)
{
	g_object_set (boxplot, "gap_percentage", (int)adj->value, NULL);
}

static gpointer
gog_box_plot_pref (GogObject *obj,
		   GogDataAllocator *dalloc, GnmCmdContext *cc)
{
	GtkWidget  *w;
	GogBoxPlot *boxplot = GOG_BOX_PLOT (obj);
	char const *dir = gnm_plugin_get_dir_name (
		plugins_get_plugin_by_id ("GOffice_plot_boxes"));
	char	 *path = g_build_filename (dir, "gog-boxplot-prefs.glade", NULL);
	GladeXML *gui = gnm_glade_xml_new (cc, path, "gog_box_plot_prefs", NULL);

	g_free (path);
        if (gui == NULL)
                return NULL;

	w = glade_xml_get_widget (gui, "gap_spinner");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (w), boxplot->gap_percentage);
	g_signal_connect (G_OBJECT (gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (w))),
		"value_changed",
		G_CALLBACK (cb_gap_changed), boxplot);

	w = glade_xml_get_widget (gui, "gog_box_plot_prefs");
	g_object_set_data_full (G_OBJECT (w),
		"state", gui, (GDestroyNotify)g_object_unref);

	return w;
}

enum {
	BOX_PLOT_PROP_0,
	BOX_PLOT_PROP_GAP_PERCENTAGE,
};

typedef struct {
	GogSeries base;
	gboolean	is_valid;
	int	 gap_percentage;
} GogBoxPlotSeries;
typedef GogSeriesClass GogBoxPlotSeriesClass;

#define GOG_BOX_PLOT_SERIES_TYPE	(gog_box_plot_series_get_type ())
#define GOG_BOX_PLOT_SERIES(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_BOX_PLOT_SERIES_TYPE, GogBoxPlotSeries))
#define IS_GOG_BOX_PLOT_SERIES(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_BOX_PLOT_SERIES_TYPE))

GType gog_box_plot_series_get_type (void);

static GogObjectClass *gog_box_plot_parent_klass;

static GType gog_box_plot_view_get_type (void);

static char const *
gog_box_plot_type_name (G_GNUC_UNUSED GogObject const *item)
{
	/* xgettext : the base for how to name box-plot objects
	 * eg The 2nd box-plot in a chart will be called
	 * 	BoxPlot2 */
	return N_("Box-Plot");
}

static void
gog_box_plot_set_property (GObject *obj, guint param_id,
			      GValue const *value, GParamSpec *pspec)
{
	GogBoxPlot *boxplot = GOG_BOX_PLOT (obj);

	switch (param_id) {
	case BOX_PLOT_PROP_GAP_PERCENTAGE:
		boxplot->gap_percentage = g_value_get_int (value);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
	gog_object_emit_changed (GOG_OBJECT (obj), TRUE);
}

static void
gog_box_plot_get_property (GObject *obj, guint param_id,
			      GValue *value, GParamSpec *pspec)
{
	GogBoxPlot *boxplot = GOG_BOX_PLOT (obj);

	switch (param_id) {
	case BOX_PLOT_PROP_GAP_PERCENTAGE:
		g_value_set_int (value, boxplot->gap_percentage);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gog_box_plot_update (GogObject *obj)
{
	GogBoxPlot *model = GOG_BOX_PLOT (obj);
	GogBoxPlotSeries *series;
	GSList *ptr;
	double *vals;
	double min, max;
	unsigned num_series = 0;

	min =  DBL_MAX;
	max = -DBL_MAX;
	for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next) {
		series = GOG_BOX_PLOT_SERIES (ptr->data);
		if (!gog_series_is_valid (GOG_SERIES (series)))
			continue;
		num_series++;
		if (!series->is_valid)
			continue;
		vals = go_data_vector_get_values (GO_DATA_VECTOR (series->base.values[0].data));
		if (vals[0] < min)
			min = vals[0];
		if (vals[4] > max)
			max = vals[4];
	}
	if (min == DBL_MAX)
		min = 0.;
	if (max == -DBL_MAX)
		max = 1.;
	if (model->min != min || model->max != max || model->num_series != num_series) {
		model->min = min;
		model->max = max;
		model->num_series = num_series;
		gog_axis_bound_changed (model->base.axis[0], GOG_OBJECT (model));
	}
	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
}

static GOData *
gog_box_plot_axis_get_bounds (GogPlot *plot, GogAxisType axis,
			      GogPlotBoundInfo *bounds)
{
	GogBoxPlot *model = GOG_BOX_PLOT (plot);

	bounds->val.minima = model->min;
	bounds->val.maxima = model->max;
	bounds->is_discrete = FALSE;

	return NULL;
}

static GogAxisSet
gog_box_plot_axis_set_pref (GogPlot const *plot)
{
	return GOG_AXIS_SET_X;
}

static gboolean
gog_box_plot_axis_set_is_valid (GogPlot const *plot, GogAxisSet type)
{
	return type == GOG_AXIS_SET_X;
}

static gboolean
gog_box_plot_axis_set_assign (GogPlot *plot, GogAxisSet type)
{
	return type == GOG_AXIS_SET_X;
}

static void
gog_box_plot_class_init (GogPlotClass *gog_plot_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) gog_plot_klass;
	GogObjectClass *gog_object_klass = (GogObjectClass *) gog_plot_klass;
	GogPlotClass   *plot_klass = (GogPlotClass *) gog_plot_klass;

	gog_box_plot_parent_klass = g_type_class_peek_parent (gog_plot_klass);

	gobject_klass->set_property = gog_box_plot_set_property;
	gobject_klass->get_property = gog_box_plot_get_property;
	g_object_class_install_property (gobject_klass, BOX_PLOT_PROP_GAP_PERCENTAGE,
		g_param_spec_int ("gap_percentage", "gap percentage",
			"The padding around each group as a percentage of their width",
			0, 500, 150, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));

	gog_object_klass->type_name	= gog_box_plot_type_name;
	gog_object_klass->view_type	= gog_box_plot_view_get_type ();
	gog_object_klass->update	= gog_box_plot_update;
	gog_object_klass->editor	= gog_box_plot_pref;

	{
		static GogSeriesDimDesc dimensions[] = {
			{ N_("Values"), GOG_SERIES_REQUIRED, FALSE,
			  GOG_DIM_VALUE, GOG_MS_DIM_VALUES },
		};
		plot_klass->desc.series.dim = dimensions;
		plot_klass->desc.series.num_dim = G_N_ELEMENTS (dimensions);
	}
	plot_klass->desc.num_series_min = 1;
	plot_klass->desc.num_series_max = G_MAXINT;
	plot_klass->series_type = gog_box_plot_series_get_type ();
	plot_klass->axis_set_pref     = gog_box_plot_axis_set_pref;
	plot_klass->axis_set_is_valid = gog_box_plot_axis_set_is_valid;
	plot_klass->axis_set_assign   = gog_box_plot_axis_set_assign;
	plot_klass->desc.series.style_fields	= GOG_STYLE_LINE | GOG_STYLE_FILL;
	plot_klass->axis_get_bounds   		= gog_box_plot_axis_get_bounds;
}

static void
gog_box_plot_init (GogBoxPlot *model)
{
	model->gap_percentage = 150;
}

GSF_CLASS (GogBoxPlot, gog_box_plot,
	   gog_box_plot_class_init, gog_box_plot_init,
	   GOG_PLOT_TYPE)

/*****************************************************************************/
typedef GogPlotView		GogBoxPlotView;
typedef GogPlotViewClass	GogBoxPlotViewClass;

static void
gog_box_plot_view_render (GogView *view, GogViewAllocation const *bbox)
{
	GogBoxPlot const *model = GOG_BOX_PLOT (view->model);
	GogAxisMap *x_map;
	GogBoxPlotSeries const *series;
	double hrect, hser, y, hbar;
	double const *vals;
	double min, qu1, med, qu3, max;
	ArtVpath	path[6];
	GogViewAllocation rect;
	GSList *ptr;
	double line_width;
	GogStyle *style;

	x_map = gog_axis_map_new (GOG_PLOT (model)->axis[0], 
				  view->allocation.x, view->allocation.w);

	if (!gog_axis_map_is_valid (x_map)) {
		gog_axis_map_free (x_map);
		return;
	}

	hser = view->allocation.h / model->num_series;
	hrect = hser / (1. + model->gap_percentage / 100.);
	y = view->allocation.y + view->allocation.h - hser / 2.;
	hrect /= 2.;
	hbar = hrect / 2.;
	path[0].code = ART_MOVETO;
	path[1].code = ART_LINETO;
	path[3].code = ART_LINETO;
	path[4].code = ART_LINETO;
	path[5].code = ART_END;
		
	for (ptr = model->base.series ; ptr != NULL ; ptr = ptr->next) {
		series = ptr->data;
		if (!gog_series_is_valid (GOG_SERIES (series)))
			continue;
		style = GOG_STYLED_OBJECT (series)->style;
		line_width = style->line.width / 2.;
		gog_renderer_push_style (view->renderer, style);
		if (series->is_valid) {
			vals = go_data_vector_get_values (
				GO_DATA_VECTOR (series->base.values[0].data));
			min = gog_axis_map_to_canvas (x_map, vals[0]);
			qu1 = gog_axis_map_to_canvas (x_map, vals[1]);
			med = gog_axis_map_to_canvas (x_map, vals[2]);
			qu3 = gog_axis_map_to_canvas (x_map, vals[3]);
			max = gog_axis_map_to_canvas (x_map, vals[4]);
			rect.x = qu1;
			rect.w = qu3 - qu1;
			rect.y = y - hrect;
			rect.h = 2* hrect;
			gog_renderer_draw_rectangle (view->renderer, &rect, NULL);
			path[2].code = ART_END;
			path[0].y = y + hbar;
			path[1].y = y - hbar;
			path[0].x = path[1].x = min;
			gog_renderer_draw_path (view->renderer, path, NULL);
			path[0].x = path[1].x = max;
			gog_renderer_draw_path (view->renderer, path, NULL);
			path[0].y = path[1].y = y;
			path[0].x = qu3;
			gog_renderer_draw_path (view->renderer, path, NULL);
			path[0].x = min;
			path[1].x = qu1;
			gog_renderer_draw_path (view->renderer, path, NULL);
			path[0].x = path[1].x = med;
			path[0].y = y + hrect;
			path[1].y = y - hrect;
			gog_renderer_draw_path (view->renderer, path, NULL);
			path[2].code = ART_LINETO;
			path[0].x = path[3].x = path[4].x = qu1;
			path[1].x = path[2].x = qu3;
			path[0].y = path[1].y = path[4].y = y - hrect;
			path[2].y = path[3].y = y + hrect;
			gog_renderer_draw_path (view->renderer, path, NULL);
		} else {
			GogViewAllocation pos;
			pos.x = view->allocation.x + view->allocation.w / 2;
			pos.y = y;
			gog_renderer_draw_text (view->renderer, _("Invalid data."), &pos,
					GTK_ANCHOR_CENTER, NULL);
			
		}
		gog_renderer_pop_style (view->renderer);
		y -= hser;
	}
}

static void
gog_box_plot_view_class_init (GogViewClass *view_klass)
{
	view_klass->render	  = gog_box_plot_view_render;
	view_klass->clip	  = TRUE;
}

static GSF_CLASS (GogBoxPlotView, gog_box_plot_view,
		  gog_box_plot_view_class_init, NULL,
		  GOG_PLOT_VIEW_TYPE)

/*****************************************************************************/

static GogObjectClass *gog_box_plot_series_parent_klass;

static void
gog_box_plot_series_update (GogObject *obj)
{
	double *vals = NULL;
	int len = 0;
	GogBoxPlotSeries *series = GOG_BOX_PLOT_SERIES (obj);
	unsigned old_num = series->base.num_elements;

	if (series->base.values[0].data != NULL) {
		vals = go_data_vector_get_values (GO_DATA_VECTOR (series->base.values[0].data));
		len = go_data_vector_get_len 
			(GO_DATA_VECTOR (series->base.values[0].data));
	}
	series->base.num_elements = len;
	series->is_valid = (len == 5) && (vals[0] <= vals[1]) && (vals[1] <= vals[2])
						 && (vals[2] <= vals[3]) && (vals[3] <= vals[4]);

	/* queue plot for redraw */
	gog_object_request_update (GOG_OBJECT (series->base.plot));
	if (old_num != series->base.num_elements)
		gog_plot_request_cardinality_update (series->base.plot);

	if (gog_box_plot_series_parent_klass->update)
		gog_box_plot_series_parent_klass->update (obj);
}

static void
gog_box_plot_series_init_style (GogStyledObject *gso, GogStyle *style)
{
	((GogStyledObjectClass*) gog_box_plot_series_parent_klass)->init_style (gso, style);

	style->outline.dash_type = GO_LINE_NONE;
}

static void
gog_box_plot_series_class_init (GogObjectClass *obj_klass)
{
	GogStyledObjectClass *gso_klass = (GogStyledObjectClass*) obj_klass;

	gog_box_plot_series_parent_klass = g_type_class_peek_parent (obj_klass);
	obj_klass->update = gog_box_plot_series_update;
	gso_klass->init_style = gog_box_plot_series_init_style;
}

GSF_CLASS (GogBoxPlotSeries, gog_box_plot_series,
	   gog_box_plot_series_class_init, NULL,
	   GOG_SERIES_TYPE)

/* Plugin initialization */

void
plugin_init (void)
{
	gog_box_plot_get_type ();
}

void
plugin_cleanup (void)
{
}
