/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-barcol.c
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
#include "gog-barcol.h"
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/gog-theme.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/go-data.h>
#include <goffice/utils/go-color.h>

#include <src/gnumeric-i18n.h>
#include <src/mathfunc.h>
#include <gsf/gsf-impl-utils.h>

enum {
	BARCOL_PROP_0,
	BARCOL_PROP_GAP_PERCENTAGE,
	BARCOL_PROP_OVERLAP_PERCENTAGE,
	BARCOL_PROP_HORIZONTAL
};

static GType gog_barcol_view_get_type (void);

static void
gog_barcol_plot_set_property (GObject *obj, guint param_id,
			      GValue const *value, GParamSpec *pspec)
{
	GogBarColPlot *barcol = GOG_BARCOL_PLOT (obj);

	switch (param_id) {
	case BARCOL_PROP_GAP_PERCENTAGE:
		barcol->gap_percentage = g_value_get_int (value);
		break;

	case BARCOL_PROP_OVERLAP_PERCENTAGE:
		barcol->overlap_percentage = g_value_get_int (value);
		break;
	case BARCOL_PROP_HORIZONTAL:
		barcol->horizontal = g_value_get_boolean (value);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
	gog_object_emit_changed (GOG_OBJECT (obj), TRUE);
}

static void
gog_barcol_plot_get_property (GObject *obj, guint param_id,
			      GValue *value, GParamSpec *pspec)
{
	GogBarColPlot *barcol = GOG_BARCOL_PLOT (obj);

	switch (param_id) {
	case BARCOL_PROP_GAP_PERCENTAGE:
		g_value_set_int (value, barcol->gap_percentage);
		break;
	case BARCOL_PROP_OVERLAP_PERCENTAGE:
		g_value_set_int (value, barcol->overlap_percentage);
		break;
	case BARCOL_PROP_HORIZONTAL:
		g_value_set_boolean (value, barcol->horizontal);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static char const *
gog_barcol_plot_type_name (G_GNUC_UNUSED GogObject const *item)
{
	/* xgettext : the base for how to name bar/col plot objects
	 * eg The 2nd bar/col plot in a chart will be called
	 * 	PlotBarCol2
	 */
	return N_("PlotBarCol");
}

extern gpointer gog_barcol_plot_pref (GogBarColPlot *barcol, CommandContext *cc);
static gpointer
gog_barcol_plot_editor (GogObject *item,
			G_GNUC_UNUSED GogDataAllocator *dalloc,
			CommandContext *cc)
{
	return gog_barcol_plot_pref (GOG_BARCOL_PLOT (item), cc);
}

static gboolean
gog_barcol_swap_x_and_y (GogPlot1_5d *model)
{
	return GOG_BARCOL_PLOT (model)->horizontal;
}

static void
gog_barcol_update_stacked_and_percentage (GogPlot1_5d *model,
					  double **vals, unsigned const *lengths)
{
	unsigned i, j;
	double neg_sum, pos_sum, tmp;

	if (GOG_1_5D_NORMAL == model->type)
		return;

	for (i = model->num_elements ; i-- > 0 ; ) {
		neg_sum = pos_sum = 0.;
		for (j = model->num_series ; j-- > 0 ; ) {
			if (i >= lengths[j])
				continue;
			tmp = vals[j][i];
			if (!finite (tmp))
				continue;
			if (tmp > 0.)
				pos_sum += tmp;
			else
				neg_sum += tmp;
		}

		if (GOG_1_5D_STACKED == model->type) {
			if (model->minimum > neg_sum)
				model->minimum = neg_sum;
			if (model->maximum < pos_sum)
				model->maximum = pos_sum;
		} else {
			if (neg_sum < 0) {
				tmp = pos_sum / (pos_sum - neg_sum);
				if (model->minimum > (tmp - 1.))
					model->minimum = tmp - 1.;
				if (model->maximum < tmp)
					model->maximum = tmp;
			} else
				model->maximum = 1.;
		}
	}
}

static void
gog_barcol_plot_class_init (GogPlot1_5dClass *gog_plot_1_5d_klass)
{
	GObjectClass   *gobject_klass = (GObjectClass *) gog_plot_1_5d_klass;
	GogObjectClass *gog_object_klass = (GogObjectClass *) gog_plot_1_5d_klass;
	GogPlotClass   *plot_klass = (GogPlotClass *) gog_plot_1_5d_klass;

	gobject_klass->set_property = gog_barcol_plot_set_property;
	gobject_klass->get_property = gog_barcol_plot_get_property;

	g_object_class_install_property (gobject_klass, BARCOL_PROP_GAP_PERCENTAGE,
		g_param_spec_int ("gap_percentage", "gap percentage",
			"The padding around each group as a percentage of their width",
			0, 500, 150, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, BARCOL_PROP_OVERLAP_PERCENTAGE,
		g_param_spec_int ("overlap_percentage", "overlap percentage",
			"The distance between series as a percentage of their width",
			-100, 100, 100, G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, BARCOL_PROP_HORIZONTAL,
		g_param_spec_boolean ("horizontal", "horizontal",
			"horizontal bars or vertical columns",
			FALSE,
			G_PARAM_READWRITE | GOG_PARAM_PERSISTENT));

	gog_object_klass->type_name	= gog_barcol_plot_type_name;
	gog_object_klass->editor	= gog_barcol_plot_editor;
	gog_object_klass->view_type	= gog_barcol_view_get_type ();

	plot_klass->desc.series.style_fields = GOG_STYLE_OUTLINE | GOG_STYLE_FILL;

	gog_plot_1_5d_klass->swap_x_and_y = gog_barcol_swap_x_and_y;
	gog_plot_1_5d_klass->update_stacked_and_percentage =
		gog_barcol_update_stacked_and_percentage;
}

static void
gog_barcol_plot_init (GogBarColPlot *model)
{
	model->gap_percentage = 150;
}

GSF_CLASS (GogBarColPlot, gog_barcol_plot,
	   gog_barcol_plot_class_init, gog_barcol_plot_init,
	   GOG_PLOT1_5D_TYPE)

/*****************************************************************************/
typedef GogPlotView		GogBarColView;
typedef GogPlotViewClass	GogBarColViewClass;

/**
 * barcol_draw_rect :
 * @rend : #GogRenderer
 * @flip :
 * @base : #GogViewAllocation
 * @rect : #GogViewAllocation
 *
 * A utility routine to build a vpath in @rect.  @rect is assumed to be in
 * coordinates relative to @base with 0,0 as the upper left.  @flip transposes
 * @rect and rotates it to put the origin in the bottom left.  Play fast and
 * loose with coordinates to keep widths >= 1.  If we allow things to be less
 * the background bleeds through.
 **/
static void
barcol_draw_rect (GogRenderer *rend, gboolean flip,
		  GogViewAllocation const *base,
		  GogViewAllocation const *rect)
{
	ArtVpath path[6];
	double base_x, base_y, w, h;

	w = rect->w;
	if (w < 1.)
		w = 1.;
	h = rect->h;
	if (h < 1.)
		h = 1.;

	/* tweak to make libart happier.  for the common case of hairline
	 * libart wants to draw 1/2 a pixel above and 1/2 below producing a
	 * fuzzy outline when antialiased. */
	if (flip) {
		base_x = base->y + base->h;
		base_y = base->x + base->w;
		path[4].x = path[1].x = path[0].x = ceil (base_x - rect->y) - .5;
		path[4].y = path[3].y = path[0].y = ceil (base_y - rect->x) - .5; 
		path[2].y = path[1].y = ceil (base_y - (rect->x + w)) - .5; 
		path[3].x = path[2].x = ceil (base_x - (rect->y + h)) - .5;
	} else {
		path[4].x = path[1].x = path[0].x = ceil (base->x + rect->x) - .5;
		path[4].y = path[3].y = path[0].y = ceil (base->y + rect->y) - .5; 
		path[2].y = path[1].y = ceil (base->y + rect->y + h) - .5; 
		path[3].x = path[2].x = ceil (base->x + rect->x + w) - .5;
	}
	path[0].code = ART_MOVETO;
	path[1].code = ART_LINETO;
	path[2].code = ART_LINETO;
	path[3].code = ART_LINETO;
	path[4].code = ART_LINETO;
	path[5].code = ART_END;

	gog_renderer_draw_polygon (rend, path, (w < 3.) || (h < 3.));
}

static void
gog_barcol_view_render (GogView *view, GogViewAllocation const *bbox)
{
	GogBarColPlot const *model = GOG_BARCOL_PLOT (view->model);
	GogPlot1_5d const *gog_1_5d_model = GOG_PLOT1_5D (view->model);
	GogSeries1_5d const *series;
	GogViewAllocation base, work;
	GogRenderer *rend = view->renderer;
	gboolean is_vertical = ! (model->horizontal);
	double **vals, sum, neg_base, pos_base, tmp;
	double col_step, group_step, scale, data_scale;
	unsigned i, j;
	unsigned num_elements = gog_1_5d_model->num_elements;
	unsigned num_series = gog_1_5d_model->num_series;
	GogPlot1_5dType const type = gog_1_5d_model->type;
	GogStyle **styles;
	GSList *ptr;
	unsigned *lengths;

	if (num_elements <= 0 || num_series <= 0 ||
	    (gnumeric_sub_epsilon (-gog_1_5d_model->minimum) < 0. &&
	     gnumeric_sub_epsilon (gog_1_5d_model->maximum) < 0.))
		return;

	vals = g_alloca (num_series * sizeof (double *));
	lengths = g_alloca (num_series * sizeof (unsigned));
	styles = g_alloca (num_series * sizeof (GogStyle *));
	i = 0;
	for (ptr = gog_1_5d_model->base.series ; ptr != NULL ; ptr = ptr->next, i++) {
		series = ptr->data;
		if (!gog_series_is_valid (GOG_SERIES (series)))
			continue;
		vals[i] = go_data_vector_get_values (
			GO_DATA_VECTOR (series->base.values[1].data));
		lengths[i] = go_data_vector_get_len (
			GO_DATA_VECTOR (series->base.values[1].data));
		styles[i] = GOG_STYLED_OBJECT (series)->style;
	}

	/* flip things */
	if (is_vertical) {
		base.x = view->allocation.y;
		base.y = view->allocation.x;
		base.w = view->allocation.h;
		base.h = view->allocation.w;
	} else
		base = view->allocation;

	/* work in coordinates drawing bars from the top */
	col_step = 1. - model->overlap_percentage / 100.;
	group_step = model->gap_percentage / 100.;
	work.h = base.h / (num_elements * (1. + ((num_series - 1) * col_step) + group_step));
	col_step *= work.h;
	group_step *= work.h;
	work.y = base.h - group_step / 2.; /* indent by half a group step */
	scale = data_scale = base.w / (gog_1_5d_model->maximum - gog_1_5d_model->minimum);

	group_step -= col_step; /* inner loop increments 1 extra time */
	for (i = 0 ; i < num_elements ; i++, work.y -= group_step) {
		pos_base = neg_base = -gog_1_5d_model->minimum * scale;
		if (type == GOG_1_5D_AS_PERCENTAGE) {
			sum = 0.;
			for (j = num_series ; j-- > 0 ; ) {
				if (i >= lengths[j])
					continue;
				tmp = vals[j][i];
				if (!finite (tmp))
					continue;
				if (tmp > 0.)
					sum += tmp;
				else
					sum -= tmp;
			}
			data_scale = scale / sum;
		}

		work.y -= work.h;
		for (j = 0 ; j < num_series ; j++, work.y -= col_step) {
			if (i >= lengths[j])
				continue;
			tmp = vals[j][i];
			if (!finite (tmp))
				continue;
			tmp *= data_scale;
			if (tmp >= 0.) {
				work.x = pos_base;
				work.w = tmp;
				if (GOG_1_5D_NORMAL != type)
					pos_base += tmp;
#warning clip
			} else {
				work.x = neg_base + tmp;
				work.w = -tmp;
				if (GOG_1_5D_NORMAL != type)
					neg_base += tmp;
#warning clip
			}

			gog_renderer_push_style (view->renderer, styles[j]);
			barcol_draw_rect (rend, is_vertical, &base, &work);
			gog_renderer_pop_style (view->renderer);
		}
	}
}

static void
gog_barcol_view_class_init (GogViewClass *view_klass)
{
	view_klass->render = gog_barcol_view_render;
}

static GSF_CLASS (GogBarColView, gog_barcol_view,
		  gog_barcol_view_class_init, NULL,
		  GOG_PLOT_VIEW_TYPE)
