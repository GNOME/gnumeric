/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-chart.c :
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
#include <goffice/graph/gog-chart-impl.h>
#include <goffice/graph/gog-plot-impl.h>
#include <goffice/graph/gog-graph-impl.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-renderer.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
#include <string.h>

enum {
	CHART_PROP_0,
	CHART_PROP_CARDINALITY_VALID
};

static GType gog_chart_view_get_type (void);
static GObjectClass *chart_parent_klass;

static char const *
gog_chart_type_name (GogObject const *obj)
{
	return N_("Chart");
}

static gpointer
gog_chart_editor (GogObject *gobj, GogDataAllocator *dalloc, CommandContext *cc)
{
	return gog_style_editor	(gobj, cc, GOG_STYLE_OUTLINE | GOG_STYLE_FILL);
}

static void
gog_chart_update (GogObject *chart)
{
	/* resets the counts */
	(void) gog_chart_get_cardinality (GOG_CHART (chart));
}

static void
role_plot_post_add (GogObject *parent, GogObject *plot)
{
	GogChart *chart = GOG_CHART (parent);

	/* APPEND to keep order, there won't be that many */
	chart->plots = g_slist_append (chart->plots, plot);
	gog_chart_request_cardinality_update (chart);

	if (chart->plots->data == plot)
		gog_chart_axis_set_assign (chart,
			gog_plot_axis_set_pref (GOG_PLOT (plot)));
	else
		gog_plot_axis_set_assign (GOG_PLOT (plot), chart->axis_set);
}

static void
role_plot_pre_remove (GogObject *parent, GogObject *plot)
{
	GogChart *chart = GOG_CHART (parent);
	chart->plots = g_slist_remove (chart->plots, plot);
	gog_chart_request_cardinality_update (chart);
}

static void
gog_chart_finalize (GObject *obj)
{
	GogChart *chart = GOG_CHART (obj);

	/* on exit the role remove routines are not called */
	g_slist_free (chart->plots);

	(chart_parent_klass->finalize) (obj);
}

static void
gog_chart_get_property (GObject *obj, guint param_id,
			GValue *value, GParamSpec *pspec)
{
	GogChart *chart = GOG_CHART (obj);
	switch (param_id) {
	case CHART_PROP_CARDINALITY_VALID:
		g_value_set_boolean (value, chart->cardinality_valid);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static gboolean
axis_can_add (GogObject const *parent, GogAxisType t)
{
	GogChart *chart = GOG_CHART (parent);
	if (chart->axis_set == GOG_AXIS_SET_UNKNOWN)
		return FALSE;
	return (chart->axis_set & (1 << t)) != 0;
}

static void
axis_pre_remove (GogObject const *child, GogAxisType t)
{
}
static gboolean x_axis_can_add (GogObject const *parent) { return axis_can_add (parent, GOG_AXIS_X); }
static void x_axis_pre_remove  (GogObject *parent, GogObject *child)  { axis_pre_remove (child, GOG_AXIS_X); }
static gboolean y_axis_can_add (GogObject const *parent) { return axis_can_add (parent, GOG_AXIS_Y); }
static void y_axis_pre_remove  (GogObject *parent, GogObject *child)  { axis_pre_remove (child, GOG_AXIS_Y); }
static gboolean z_axis_can_add (GogObject const *parent) { return axis_can_add (parent, GOG_AXIS_Z); }
static void z_axis_pre_remove  (GogObject *parent, GogObject *child)  { axis_pre_remove (child, GOG_AXIS_Z); }

static void
gog_chart_class_init (GogObjectClass *gog_klass)
{
	static GogObjectRole const roles[] = {
		{ N_("Plot"), "GogPlot",
		  GOG_POSITION_SPECIAL, GOG_POSITION_SPECIAL, FALSE,
		  NULL, NULL, NULL,
		  role_plot_post_add, role_plot_pre_remove, NULL },
		{ N_("Legend"), "GogLegend",
		  GOG_POSITION_COMPASS, GOG_POSITION_E|GOG_POSITION_ALIGN_CENTER, TRUE,
		  NULL, NULL, NULL, NULL, NULL, NULL },
		{ N_("Title"), "GogLabel",
		  GOG_POSITION_COMPASS, GOG_POSITION_N|GOG_POSITION_ALIGN_CENTER, FALSE,
		  NULL, NULL, NULL, NULL, NULL, NULL },
		{ N_("X-Axis"), "GogAxis",
		  GOG_POSITION_SPECIAL, GOG_POSITION_SPECIAL, FALSE,
		  x_axis_can_add, NULL, NULL, NULL, x_axis_pre_remove, NULL },
		{ N_("Y-Axis"), "GogAxis",
		  GOG_POSITION_SPECIAL, GOG_POSITION_SPECIAL, FALSE,
		  y_axis_can_add, NULL, NULL, NULL, y_axis_pre_remove, NULL },
		{ N_("Z-Axis"), "GogAxis",
		  GOG_POSITION_SPECIAL, GOG_POSITION_SPECIAL, FALSE,
		  z_axis_can_add, NULL, NULL, NULL, z_axis_pre_remove, NULL },
	};
	GObjectClass *gobject_klass = (GObjectClass *)gog_klass;

	chart_parent_klass = g_type_class_peek_parent (gog_klass);
	gobject_klass->finalize = gog_chart_finalize;
	gobject_klass->get_property = gog_chart_get_property;
	g_object_class_install_property (gobject_klass, CHART_PROP_CARDINALITY_VALID,
		g_param_spec_boolean ("cardinality-valid", "cardinality-valid",
			"Is the charts cardinality currently vaid",
			FALSE, G_PARAM_READABLE));

	gog_klass->editor    = gog_chart_editor;
	gog_klass->type_name = gog_chart_type_name;
	gog_klass->view_type = gog_chart_view_get_type ();
	gog_klass->update    = gog_chart_update;
	gog_object_register_roles (gog_klass, roles, G_N_ELEMENTS (roles));
}

static void
gog_chart_init (GogChart *chart)
{
	chart->x     = 0;
	chart->y     = 0;
	chart->cols  = 0;
	chart->rows  = 0;
	/* start as true so that we can queue an update when it changes */
	chart->cardinality_valid = TRUE;
	chart->axis_set = GOG_AXIS_SET_UNKNOWN;
}

GSF_CLASS (GogChart, gog_chart,
	   gog_chart_class_init, gog_chart_init,
	   GOG_STYLED_OBJECT_TYPE)

/**
 * gog_chart_get_position :
 * @chart : const #GogChart
 * @x :
 * @y :
 * @cols :
 * @rows :
 *
 * Returns TRUE if the chart has been positioned.
 **/
gboolean
gog_chart_get_position (GogChart const *chart,
			unsigned *x, unsigned *y, unsigned *cols, unsigned *rows)
{
	g_return_val_if_fail (GOG_CHART (chart), FALSE);

	if (chart->cols <= 0 || chart->rows <= 0)
		return FALSE;

	if (x != NULL)	  *x	= chart->x;
	if (y != NULL)	  *y	= chart->y;
	if (cols != NULL) *cols	= chart->cols;
	if (rows != NULL) *rows	= chart->rows;

	return TRUE;
}

/**
 * gog_chart_set_position :
 * @chart : #GogChart
 * @x :
 * @y :
 * @cols :
 * @rows :
 *
 **/
void
gog_chart_set_position (GogChart *chart,
			unsigned x, unsigned y, unsigned cols, unsigned rows)
{
	g_return_if_fail (GOG_CHART (chart) != NULL);

	if (chart->x == x && chart->y == y &&
	    chart->cols == cols && chart->rows == rows)
		return;

	chart->x = x;
	chart->y = y;
	chart->cols = cols;
	chart->rows = rows;

	gog_graph_validate_chart_layout (GOG_GRAPH (GOG_OBJECT (chart)->parent));
	gog_object_emit_changed (GOG_OBJECT (chart), TRUE);
}

unsigned
gog_chart_get_cardinality (GogChart *chart)
{
	GSList *ptr;

	g_return_val_if_fail (GOG_CHART (chart) != NULL, 0);

	if (!chart->cardinality_valid) {
		chart->cardinality_valid = TRUE;
		chart->cardinality = 0;
		for (ptr = chart->plots ; ptr != NULL ; ptr = ptr->next)
			chart->cardinality += gog_plot_get_cardinality (ptr->data);
	}
	return chart->cardinality;
}

void
gog_chart_request_cardinality_update (GogChart *chart)
{
	g_return_if_fail (GOG_CHART (chart) != NULL);
	
	if (chart->cardinality_valid) {
		chart->cardinality_valid = FALSE;
		g_object_notify (G_OBJECT (chart), "cardinality-valid");
		gog_object_request_update (GOG_OBJECT (chart));
	}
}

void
gog_chart_foreach_elem (GogChart *chart, GogEnumFunc handler, gpointer data)
{
	GSList *ptr;

	g_return_if_fail (GOG_CHART (chart) != NULL);
	g_return_if_fail (chart->cardinality_valid);

	for (ptr = chart->plots ; ptr != NULL ; ptr = ptr->next)
		gog_plot_foreach_elem (ptr->data, handler, data);
}

gboolean
gog_chart_axis_set_is_valid (GogChart const *chart, GogAxisSet type)
{
	GSList *ptr;

	g_return_val_if_fail (GOG_CHART (chart) != NULL, FALSE);

	for (ptr = chart->plots ; ptr != NULL ; ptr = ptr->next)
		if (!gog_plot_axis_set_is_valid (ptr->data, type))
			return FALSE;
	return TRUE;
}

gboolean
gog_chart_axis_set_assign (GogChart *chart, GogAxisSet type)
{
	GSList *ptr;

	g_return_val_if_fail (GOG_CHART (chart) != NULL, FALSE);

	for (ptr = chart->plots ; ptr != NULL ; ptr = ptr->next)
		if (!gog_plot_axis_set_assign (ptr->data, type))
			return FALSE;
	chart->axis_set = type;

	/* remove any existing axis that do not fit this scheme */
	for (ptr = chart->plots ; ptr != NULL ; ptr = ptr->next)
	/* Add at least 1 instance of any required axis */
	return TRUE;
}

/*********************************************************************/

typedef GogView		GogChartView;
typedef GogViewClass	GogChartViewClass;

#define GOG_CHART_VIEW_TYPE	(gog_chart_view_get_type ())
#define GOG_CHART_VIEW(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_CHART_VIEW_TYPE, GogChartView))
#define IS_GOG_CHART_VIEW(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_CHART_VIEW_TYPE))

static GogViewClass *cview_parent_klass;

static void
gog_chart_view_size_allocate (GogView *view, GogViewAllocation const *allocation)
{
	GSList *ptr;
	GogView *child;
	GogChart *chart = GOG_CHART (view->model);
	GogViewAllocation res = *allocation;
	double outline = gog_renderer_outline_size (
		view->renderer, chart->base.style);

	res.x += outline;
	res.y += outline;
	res.w -= outline * 2.;
	res.h -= outline * 2.;
	(cview_parent_klass->size_allocate) (view, &res);

	/* overlay all the plots in the residual */
	for (ptr = view->children; ptr != NULL ; ptr = ptr->next) {
		child = ptr->data;
		if (child->model->position == GOG_POSITION_SPECIAL)
			gog_view_size_allocate (child, &view->residual);
	}
}

static void
gog_chart_view_render (GogView *view, GogViewAllocation const *bbox)
{
	GogChart *chart = GOG_CHART (view->model);
	gog_renderer_push_style (view->renderer, chart->base.style);
	gog_renderer_draw_rectangle (view->renderer, &view->allocation);
	gog_renderer_pop_style (view->renderer);
	(cview_parent_klass->render) (view, bbox);
}

static void
gog_chart_view_class_init (GogChartViewClass *gview_klass)
{
	GogViewClass *view_klass    = (GogViewClass *) gview_klass;

	cview_parent_klass = g_type_class_peek_parent (gview_klass);
	view_klass->size_allocate   = gog_chart_view_size_allocate;
	view_klass->render	    = gog_chart_view_render;
}

static GSF_CLASS (GogChartView, gog_chart_view,
	   gog_chart_view_class_init, NULL,
	   GOG_VIEW_TYPE)
