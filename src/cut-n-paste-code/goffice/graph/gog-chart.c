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
#include <goffice/graph/gog-axis.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/utils/go-units.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
#include <string.h>
#include <math.h>

enum {
	CHART_PROP_0,
	CHART_PROP_PADDING_PTS,
	CHART_PROP_CARDINALITY_VALID
};

static GType gog_chart_view_get_type (void);
static GObjectClass *chart_parent_klass;

static gpointer
gog_chart_editor (GogObject *gobj, GogDataAllocator *dalloc, CommandContext *cc)
{
	return gog_style_editor	(gobj, cc, NULL, GOG_STYLE_OUTLINE | GOG_STYLE_FILL);
}

static void
gog_chart_update (GogObject *chart)
{
	/* resets the counts */
	(void) gog_chart_get_cardinality (GOG_CHART (chart));
}

static void
gog_chart_finalize (GObject *obj)
{
	GogChart *chart = GOG_CHART (obj);

	/* on exit the role remove routines are not called */
	g_slist_free (chart->plots);
	g_slist_free (chart->axes);

	(chart_parent_klass->finalize) (obj);
}

static void
gog_chart_set_property (GObject *obj, guint param_id,
			GValue const *value, GParamSpec *pspec)
{
	GogChart *chart = GOG_CHART (obj);
	switch (param_id) {
	case CHART_PROP_PADDING_PTS :
		chart->padding_pts = g_value_get_double (value);
		gog_object_emit_changed (GOG_OBJECT (obj), TRUE);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
}

static void
gog_chart_get_property (GObject *obj, guint param_id,
			GValue *value, GParamSpec *pspec)
{
	GogChart *chart = GOG_CHART (obj);
	switch (param_id) {
	case CHART_PROP_PADDING_PTS :
		g_value_set_double (value, chart->padding_pts);
		break;
	case CHART_PROP_CARDINALITY_VALID:
		g_value_set_boolean (value, chart->cardinality_valid);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
role_plot_post_add (GogObject *parent, GogObject *plot)
{
	GogChart *chart = GOG_CHART (parent);
	gboolean ok = TRUE;

	/* APPEND to keep order, there won't be that many */
	chart->plots = g_slist_append (chart->plots, plot);
	gog_chart_request_cardinality_update (chart);

	if (chart->plots->next == NULL)
		ok = gog_chart_axis_set_assign (chart,
			gog_plot_axis_set_pref (GOG_PLOT (plot)));
	ok |= gog_plot_axis_set_assign (GOG_PLOT (plot),
		chart->axis_set);

	/* a quick post condition to keep us on our toes */
	g_return_if_fail (ok);
}

static void
role_plot_pre_remove (GogObject *parent, GogObject *plot)
{
	GogChart *chart = GOG_CHART (parent);
	gog_plot_axis_clear (GOG_PLOT (plot), GOG_AXIS_SET_ALL);
	chart->plots = g_slist_remove (chart->plots, plot);
	gog_chart_request_cardinality_update (chart);
}

static gboolean
axis_can_add (GogObject const *parent, GogAxisType t)
{
	GogChart *chart = GOG_CHART (parent);
	if (chart->axis_set == GOG_AXIS_SET_UNKNOWN)
		return FALSE;
	return (chart->axis_set & (1 << t)) != 0;
}
static gboolean
axis_can_remove (GogObject const *child)
{
	return NULL == gog_axis_contributors (GOG_AXIS (child));
}

static void
axis_post_add (GogObject *axis, GogAxisType t)
{
	GogChart *chart = GOG_CHART (axis->parent);
	g_object_set (G_OBJECT (axis), "type", (int)t, NULL);
	chart->axes = g_slist_prepend (chart->axes, axis);
}

static void
axis_pre_remove (GogObject *parent, GogObject *axis)
{
	GogChart *chart = GOG_CHART (parent);
	gog_axis_clear_contributors (GOG_AXIS (axis));
	chart->axes = g_slist_remove (chart->axes, axis);
}

static gboolean x_axis_can_add (GogObject const *parent) { return axis_can_add (parent, GOG_AXIS_X); }
static void x_axis_post_add    (GogObject *parent, GogObject *child)  { axis_post_add   (child, GOG_AXIS_X); }
static gboolean y_axis_can_add (GogObject const *parent) { return axis_can_add (parent, GOG_AXIS_Y); }
static void y_axis_post_add    (GogObject *parent, GogObject *child)  { axis_post_add   (child, GOG_AXIS_Y); }
static gboolean z_axis_can_add (GogObject const *parent) { return axis_can_add (parent, GOG_AXIS_Z); }
static void z_axis_post_add    (GogObject *parent, GogObject *child)  { axis_post_add   (child, GOG_AXIS_Z); }

static GogObjectRole const roles[] = {
	{ N_("Legend"), "GogLegend",	0,
	  GOG_POSITION_COMPASS, GOG_POSITION_E|GOG_POSITION_ALIGN_CENTER, GOG_OBJECT_NAME_BY_ROLE,
	  NULL, NULL, NULL, NULL, NULL, NULL, { -1 } },
	{ N_("Title"), "GogLabel",	1,
	  GOG_POSITION_COMPASS, GOG_POSITION_N|GOG_POSITION_ALIGN_CENTER, GOG_OBJECT_NAME_BY_ROLE,
	  NULL, NULL, NULL, NULL, NULL, NULL, { -1 } },
	{ N_("X-Axis"), "GogAxis",	0,
	  GOG_POSITION_SPECIAL, GOG_POSITION_SPECIAL, GOG_OBJECT_NAME_BY_ROLE,
	  x_axis_can_add, axis_can_remove, NULL, x_axis_post_add, axis_pre_remove, NULL,
	  { GOG_AXIS_X } },
	{ N_("Y-Axis"), "GogAxis",	1,
	  GOG_POSITION_SPECIAL, GOG_POSITION_SPECIAL, GOG_OBJECT_NAME_BY_ROLE,
	  y_axis_can_add, axis_can_remove, NULL, y_axis_post_add, axis_pre_remove, NULL,
	  { GOG_AXIS_Y } },
	{ N_("Z-Axis"), "GogAxis",	2,
	  GOG_POSITION_SPECIAL, GOG_POSITION_SPECIAL, GOG_OBJECT_NAME_BY_ROLE,
	  z_axis_can_add, axis_can_remove, NULL, z_axis_post_add, axis_pre_remove, NULL,
	  { GOG_AXIS_Z } },
	{ N_("Plot"), "GogPlot",	3,	/* keep the axis before the plots */
	  GOG_POSITION_SPECIAL, GOG_POSITION_SPECIAL, GOG_OBJECT_NAME_BY_TYPE,
	  NULL, NULL, NULL, role_plot_post_add, role_plot_pre_remove, NULL, { -1 } }
};
static void
gog_chart_class_init (GogObjectClass *gog_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *)gog_klass;

	chart_parent_klass = g_type_class_peek_parent (gog_klass);
	gobject_klass->finalize = gog_chart_finalize;
	gobject_klass->set_property = gog_chart_set_property;
	gobject_klass->get_property = gog_chart_get_property;

	g_object_class_install_property (gobject_klass, CHART_PROP_PADDING_PTS,
		g_param_spec_double ("padding_pts", "Padding Pts",
			"# of pts separating charts in the grid.",
			0, G_MAXDOUBLE, 0, G_PARAM_READWRITE|GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, CHART_PROP_CARDINALITY_VALID,
		g_param_spec_boolean ("cardinality-valid", "cardinality-valid",
			"Is the charts cardinality currently vaid",
			FALSE, G_PARAM_READABLE));

	gog_klass->editor    = gog_chart_editor;
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
	chart->padding_pts = GO_CM_TO_PT (.25);

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

static void
gog_chart_add_axis (GogChart *chart, GogAxisType type)
{
	unsigned i = G_N_ELEMENTS (roles);
	while (i-- > 0)
		if (roles[i].user.i == (int)type) {
			gog_object_add_by_role (GOG_OBJECT (chart), roles + i, NULL);
			return;
		}
	g_warning ("unknown axis type %d", type);
}

gboolean
gog_chart_axis_set_assign (GogChart *chart, GogAxisSet axis_set)
{
	GogAxis *axis;
	GSList  *ptr;
	GogAxisType type;

	g_return_val_if_fail (GOG_CHART (chart) != NULL, FALSE);

	if (chart->axis_set == axis_set)
		return TRUE;
	chart->axis_set = axis_set;

	/* Add at least 1 instance of any required axis */
	for (type = 0 ; type < GOG_AXIS_TYPES ; type++)
		if ((axis_set & (1 << type))) {
			GSList *tmp = gog_chart_get_axis (chart, type);
			if (tmp == NULL)
				gog_chart_add_axis (chart, type);
			else
				g_slist_free (tmp);
		}

	/* link the plots */
	for (ptr = chart->plots ; ptr != NULL ; ptr = ptr->next)
		if (!gog_plot_axis_set_assign (ptr->data, axis_set))
			return FALSE;

	/* remove any existing axis that do not fit this scheme */
	for (ptr = GOG_OBJECT (chart)->children ; ptr != NULL ; ) {
		axis = ptr->data;
		ptr = ptr->next; /* list may change under us */
		if (IS_GOG_AXIS (axis)) {
			type = -1;
			g_object_get (G_OBJECT (axis), "type", &type, NULL);
			if (type < 0 || type >= GOG_AXIS_TYPES) {
				g_warning ("Invalid axis");
				continue;
			}

			if (0 == (axis_set & (1 << type))) {
				gog_object_clear_parent (GOG_OBJECT (axis));
				g_object_unref (axis);
			}
		}
	}

	return TRUE;
}

/**
 * gog_chart_get_axis :
 * @chart : #GogChart
 * @target  : #GogAxisType
 *
 * Return a list which the caller must free of all axis of type @target
 * associated with @chart.
 **/
GSList *
gog_chart_get_axis (GogChart const *chart, GogAxisType target)
{
	GSList *ptr, *res = NULL;
	GogAxis *axis;
	int type;

	g_return_val_if_fail (GOG_CHART (chart) != NULL, NULL);

	for (ptr = GOG_OBJECT (chart)->children ; ptr != NULL ; ptr = ptr->next) {
		axis = ptr->data;
		if (IS_GOG_AXIS (axis)) {
			type = -1;
			g_object_get (G_OBJECT (axis), "type", &type, NULL);
			if (type < 0 || type >= GOG_AXIS_TYPES) {
				g_warning ("Invalid axis");
				continue;
			}
			if (type == target)
				res = g_slist_prepend (res, axis);
		}
	}

	return res;
}

/*********************************************************************/

typedef struct {
	GogView base;

	/* indents */
	double pre_x, post_x;
} GogChartView;
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
	GogChartView	*cview = GOG_CHART_VIEW (view);
	GogChart	*chart = GOG_CHART (view->model);
	GogAxis const *axis;
	GogViewAllocation tmp, res = *allocation;
	GogViewRequisition req;
	double outline = gog_renderer_line_size (
		view->renderer, chart->base.style->outline.width);
	outline += gog_renderer_line_size (
		view->renderer, chart->padding_pts);

	res.x += outline;
	res.y += outline;
	res.w -= outline * 2.;
	res.h -= outline * 2.;
	(cview_parent_klass->size_allocate) (view, &res);

	res = view->residual;
	if (chart->axis_set == GOG_AXIS_SET_XY) {
		/* position the X & Y axes */
		double pre_x = 0., post_x = 0., pre_y = 0, post_y = 0.;
		double old_pre_x, old_post_x, old_pre_y, old_post_y;

		do {
			old_pre_x  = pre_x;	old_post_x = post_x;
			old_pre_y  = pre_y;	old_post_y = post_y;
			pre_x = post_x = pre_y = post_y = 0.;
			for (ptr = view->children; ptr != NULL ; ptr = ptr->next) {
				child = ptr->data;
				if (child->model->position != GOG_POSITION_SPECIAL ||
				    !IS_GOG_AXIS (child->model))
					continue;

				axis = GOG_AXIS (child->model);

				req.w = res.w - pre_x - post_x;
				req.h = res.h - pre_y - post_y;
				gog_view_size_request (child, &req);

				switch (gog_axis_get_atype (axis)) {
				case GOG_AXIS_X:
					/* X axis fill the bottom and top */
					tmp.x = res.x;
					tmp.w = res.w;
					tmp.h = req.h;
					switch (gog_axis_get_pos (axis)) {
						case GOG_AXIS_AT_LOW:
							post_y += req.h;
							tmp.y   = res.y + res.h - post_y;
							break;
						case GOG_AXIS_AT_HIGH:
							tmp.y  = res.y + pre_y;
							pre_y += req.h;
							break;
						default:
							break;
					}
				break;
				case GOG_AXIS_Y:
					/* Y axis take just the previous middle,
					 * if it changes we'll iterate back */
					tmp.y = res.y + old_pre_y;
					tmp.h = res.h - old_pre_y - old_post_y;
					tmp.w = req.w;
					switch (gog_axis_get_pos (axis)) {
						case GOG_AXIS_AT_LOW:
							tmp.x = res.x + pre_x;
							pre_x  += req.w;
							break;
						case GOG_AXIS_AT_HIGH:
							post_x  += req.w;
							tmp.x = res.x + res.w - post_x;
							break;
						default:
							break;
					}
					break;

				default: break;
				}
				gog_view_size_allocate (child, &tmp);
			}
		/* as things get narrower their size may change */
		} while (fabs (old_pre_x - pre_x) > 1e-4 ||
			 fabs (old_post_x- post_x) > 1e-4 ||
			 fabs (old_pre_y - pre_y) > 1e-4 ||
			 fabs (old_post_y- post_y) > 1e-4);

		cview->pre_x  = pre_x;
		cview->post_x = post_x;
		res.x += pre_x;	res.w -= pre_x + post_x;
		res.y += pre_y;	res.h -= pre_y + post_y;
	} else if (chart->axis_set > GOG_AXIS_SET_NONE) { /* catch unknown or none */
		g_warning ("only have layout engine for xy and none currently");
		return;
	}

	/* overlay all the plots in the residual */
	for (ptr = view->children; ptr != NULL ; ptr = ptr->next) {
		child = ptr->data;
		if (child->model->position == GOG_POSITION_SPECIAL &&
		    IS_GOG_PLOT (child->model))
			gog_view_size_allocate (child, &res);
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
gog_chart_view_init (GogChartView *cview)
{
	cview->pre_x = cview->post_x = 0.;
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
		  gog_chart_view_class_init, gog_chart_view_init,
		  GOG_VIEW_TYPE)

/**
 * gog_chart_view_get_indents :
 * @view : #GogChartView, publicly a GogView to keep this type private
 * @pre	 :
 * @post :
 *
 * Get the indentation necessary to align the lines for axis from different
 * dimensions.
 **/
void
gog_chart_view_get_indents (GogView const *view, double *pre, double *post)
{
	GogChartView *cview = GOG_CHART_VIEW (view);

	g_return_if_fail (cview != NULL);

	*pre = cview->pre_x;
	*post = cview->post_x;
}
