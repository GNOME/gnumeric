/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-renderer.c :
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
#include <goffice/graph/gog-renderer-impl.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-graph.h>
#include <goffice/graph/gog-view.h>
#include <goffice/utils/go-units.h>

#include <gsf/gsf-impl-utils.h>

enum {
	RENDERER_PROP_0,
	RENDERER_PROP_MODEL,
	RENDERER_PROP_VIEW,
	RENDERER_PROP_LOGICAL_WIDTH_PTS,
	RENDERER_PROP_LOGICAL_HEIGHT_PTS
};
enum {
	RENDERER_SIGNAL_REQUEST_UPDATE,
	RENDERER_SIGNAL_LAST
};
static gulong renderer_signals [RENDERER_SIGNAL_LAST] = { 0, };

static GObjectClass *parent_klass;

static void
gog_renderer_finalize (GObject *obj)
{
	GogRenderer *rend = GOG_RENDERER (obj);

	if (rend->cur_style != NULL) {
		g_warning ("Missing calls to gog_renderer_style_pop left dangling style references");
		g_slist_foreach (rend->style_stack,
			(GFunc)g_object_unref, NULL);
		g_slist_free (rend->style_stack);
		rend->style_stack = NULL;
		g_object_unref (rend->cur_style);
		rend->cur_style = NULL;
	}

	if (rend->view != NULL) {
		g_object_unref (rend->view);
		rend->view = NULL;
	}

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static void
gog_renderer_set_property (GObject *obj, guint param_id,
			   GValue const *value, GParamSpec *pspec)
{
	GogRenderer *rend = GOG_RENDERER (obj);
	double tmp;

	switch (param_id) {
	case RENDERER_PROP_MODEL:
		rend->model = GOG_GRAPH (g_value_get_object (value));
		if (rend->view != NULL)
			g_object_unref (rend->view);
		rend->view = g_object_new (gog_graph_view_get_type (),
					   "renderer",	rend,
					   "model",	rend->model,
					   NULL);
		gog_renderer_request_update (rend);
		break;
	case RENDERER_PROP_LOGICAL_WIDTH_PTS:
		tmp = g_value_get_double (value);
		if (tmp != rend->logical_width_pts)
			return;
		rend->logical_width_pts = tmp;
		break;

	case RENDERER_PROP_LOGICAL_HEIGHT_PTS:
		tmp = g_value_get_double (value);
		if (tmp != rend->logical_height_pts)
			return;
		rend->logical_height_pts = tmp;
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
}

static void
gog_renderer_get_property (GObject *obj, guint param_id,
			   GValue *value, GParamSpec *pspec)
{
	GogRenderer *rend = GOG_RENDERER (obj);

	switch (param_id) {
	case RENDERER_PROP_MODEL:
		g_value_set_object (value, rend->model);
		break;
	case RENDERER_PROP_VIEW:
		g_value_set_object (value, rend->view);
		break;
	case RENDERER_PROP_LOGICAL_WIDTH_PTS:
		g_value_set_double (value, rend->logical_width_pts);
		break;
	case RENDERER_PROP_LOGICAL_HEIGHT_PTS:
		g_value_set_double (value, rend->logical_height_pts);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

void
gog_renderer_request_update (GogRenderer *renderer)
{
	g_return_if_fail (GOG_RENDERER (renderer) != NULL);

	if (renderer->needs_update)
		return;
	renderer->needs_update = TRUE;
	g_signal_emit (G_OBJECT (renderer),
		renderer_signals [RENDERER_SIGNAL_REQUEST_UPDATE], 0);
}

void
gog_renderer_begin_drawing (GogRenderer *rend)
{
	GogRendererClass *klass = GOG_RENDERER_GET_CLASS (rend);
	g_return_if_fail (klass != NULL);
	if (klass->begin_drawing != NULL)
		(klass->begin_drawing) (rend);
}

void
gog_renderer_end_drawing (GogRenderer *rend)
{
	GogRendererClass *klass = GOG_RENDERER_GET_CLASS (rend);
	g_return_if_fail (klass != NULL);
	if (klass->end_drawing != NULL)
		(klass->end_drawing) (rend);
}

void
gog_renderer_push_style (GogRenderer *rend, GogStyle *style)
{
	GogRendererClass *klass = GOG_RENDERER_GET_CLASS (rend);

	g_return_if_fail (klass != NULL);
	g_return_if_fail (GOG_STYLE (style) != NULL);

	if (rend->cur_style != NULL)
		rend->style_stack = g_slist_prepend (
			rend->style_stack, rend->cur_style);
	g_object_ref (style);
	rend->cur_style = style;

	if (klass->push_style)
		klass->push_style (rend, style);
}

void
gog_renderer_pop_style (GogRenderer *rend)
{
	GogRendererClass *klass = GOG_RENDERER_GET_CLASS (rend);

	g_return_if_fail (klass != NULL);
	g_return_if_fail (rend->cur_style != NULL);

	g_object_unref (rend->cur_style);
	if (rend->style_stack != NULL) {
		rend->cur_style = rend->style_stack->data;
		rend->style_stack = g_slist_remove (rend->style_stack,
			rend->cur_style);
	} else
		rend->cur_style = NULL;

	if (klass->pop_style)
		klass->pop_style (rend);
}

void
gog_renderer_draw_path (GogRenderer *rend, ArtVpath *path)
{
	GogRendererClass *klass = GOG_RENDERER_GET_CLASS (rend);

	g_return_if_fail (klass != NULL);
	g_return_if_fail (rend->cur_style != NULL);

	(klass->draw_path) (rend, path);
}

void
gog_renderer_draw_polygon (GogRenderer *rend, ArtVpath *path, gboolean narrow)
{
	GogRendererClass *klass = GOG_RENDERER_GET_CLASS (rend);

	g_return_if_fail (klass != NULL);
	g_return_if_fail (rend->cur_style != NULL);

	(klass->draw_polygon) (rend, path, narrow);
}

static void
gog_renderer_class_init (GogRendererClass *renderer_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *)renderer_klass;

	parent_klass = g_type_class_peek_parent (gobject_klass);
	gobject_klass->finalize	    = gog_renderer_finalize;
	gobject_klass->set_property = gog_renderer_set_property;
	gobject_klass->get_property = gog_renderer_get_property;

	g_object_class_install_property (gobject_klass, RENDERER_PROP_MODEL,
		g_param_spec_object ("model", "model",
			"the GogGraph this renderer displays",
			GOG_GRAPH_TYPE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_klass, RENDERER_PROP_VIEW,
		g_param_spec_object ("view", "view",
			"the GogView this renderer is displaying",
			GOG_VIEW_TYPE, G_PARAM_READABLE));
	g_object_class_install_property (gobject_klass, RENDERER_PROP_LOGICAL_WIDTH_PTS,
		g_param_spec_double ("logical_width_pts", "Logical Width Pts",
			"Logical width of the drawing area in pts",
			0, G_MAXDOUBLE, 0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_klass, RENDERER_PROP_LOGICAL_HEIGHT_PTS,
		g_param_spec_double ("logical_height_pts", "Logical Height Pts",
			"Logical height of the drawing area in pts",
			0, G_MAXDOUBLE, 0, G_PARAM_READWRITE));

	renderer_signals [RENDERER_SIGNAL_REQUEST_UPDATE] = g_signal_new ("request_update",
		G_TYPE_FROM_CLASS (renderer_klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GogRendererClass, request_update),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

static void
gog_renderer_init (GogRenderer *rend)
{
	rend->needs_update = FALSE;
	rend->cur_style    = NULL;
	rend->style_stack  = NULL;
	rend->scale = rend->scale_x = rend->scale_y = 1.;
	rend->logical_width_pts    = GO_CM_TO_PT (6);
	rend->logical_height_pts   = GO_CM_TO_PT (6);
}

GSF_CLASS (GogRenderer, gog_renderer,
	   gog_renderer_class_init, gog_renderer_init,
	   G_TYPE_OBJECT)

/**
 * gog_renderer_draw_rectangle :
 * @renderer : #GogRenderer
 * @rect : #GogViewAllocation
 *
 * A utility routine to build a vpath in @rect.
 **/
void
gog_renderer_draw_rectangle (GogRenderer *rend, GogViewAllocation const *rect)
{
	ArtVpath path[6];

	path[0].code = ART_MOVETO;
	path[0].x = rect->x;
	path[0].y = rect->y; 
	path[1].code = ART_LINETO;
	path[1].x = rect->x;
	path[1].y = rect->y + rect->h; 
	path[2].code = ART_LINETO;
	path[2].x = rect->x + rect->w;
	path[2].y = rect->y + rect->h; 
	path[3].code = ART_LINETO;
	path[3].x = rect->x + rect->w;
	path[3].y = rect->y; 
	path[4].code = ART_LINETO;
	path[4].x = rect->x;
	path[4].y = rect->y; 
	path[5].code = ART_END;

	gog_renderer_draw_polygon (rend, path, (rect->w < 3.) || (rect->h < 3.));
}

double
gog_renderer_outline_size (GogRenderer *rend, GogStyle *style)
{
	double width;

	g_return_val_if_fail (style != NULL, 0.);

	width = style->outline.width;
	if (width < 1.) /* cheesy version of hairline */
		return 1.;
	return width * rend->scale;
}

double
gog_renderer_pt2r_x (GogRenderer *rend, double d)
{
	return d * rend->scale_x;
}

double
gog_renderer_pt2r_y (GogRenderer *rend, double d)
{
	return d * rend->scale_y;
}

double
gog_renderer_pt2r (GogRenderer *rend, double d)
{
	return d * rend->scale;
}
