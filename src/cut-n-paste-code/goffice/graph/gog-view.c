/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-view.c :
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
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-renderer.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>

enum {
	GOG_VIEW_PROP_0,
	GOG_VIEW_PROP_PARENT,
	GOG_VIEW_PROP_MODEL
};

static GObjectClass *parent_klass;

static void
cb_child_added (GogObject *parent, GogObject *child,
		GogView *view)
{
	g_return_if_fail (view->model == parent);

	gog_object_new_view (child, view);
	gog_view_queue_resize (view);
}

static void
cb_remove_child (GogObject *parent, GogObject *child,
		 GogView *view)
{
	GSList *ptr = view->children;
	GogView *tmp;

	g_return_if_fail (view->model == parent);

	gog_view_queue_resize (view);
	for (; ptr != NULL ; ptr = ptr->next) {
		tmp = GOG_VIEW (ptr->data);

		g_return_if_fail (tmp != NULL);

		if (tmp->model == child) {
			g_object_unref (tmp);
			return;
		}
	}
	g_warning ("%s (%p) saw %s(%p) being removed from %s(%p) which wasn't my child",
		   G_OBJECT_TYPE_NAME (view), view,
		   G_OBJECT_TYPE_NAME (child), child,
		   G_OBJECT_TYPE_NAME (parent), parent);
}

static void
cb_model_changed (GogObject *model, gboolean resized, GogView *view)
{
	g_warning ("model %s(%p) for view %s(%p) changed %d",
		   G_OBJECT_TYPE_NAME (model), model,
		   G_OBJECT_TYPE_NAME (view), view, resized);
	if (resized)
		gog_view_queue_resize (view);
	else
		gog_view_queue_redraw (view);
}

/* make the list of view children match the models order */
static void
cb_model_reordered (GogView *view)
{
	GSList *tmp, *new_order = NULL;
	GSList *ptr = view->model->children;

	for (; ptr != NULL ; ptr = ptr->next) {
		tmp = view->children;
		/* not all the views may be created yet check for NULL */
		while (tmp != NULL && GOG_VIEW (tmp->data)->model != ptr->data)
			tmp = tmp->next;
		if (tmp != NULL)
			new_order = g_slist_prepend (new_order, tmp->data);
	}
	g_slist_free (view->children);
	view->children = g_slist_reverse (new_order);
}

static void
gog_view_set_property (GObject *gobject, guint param_id,
		       GValue const *value, GParamSpec *pspec)
{
	GogView *view = GOG_VIEW (gobject);
	gboolean init_state = (view->renderer == NULL || view->model == NULL);

	switch (param_id) {
	case GOG_VIEW_PROP_PARENT:
		g_return_if_fail (view->parent == NULL);

		view->parent = GOG_VIEW (g_value_get_object (value));
		if (view->parent != NULL) {
			view->renderer = view->parent->renderer;
			view->parent->children = g_slist_prepend (view->parent->children, view);
			cb_model_reordered (view->parent);
		}
		break;

	case GOG_VIEW_PROP_MODEL:
		g_return_if_fail (view->model == NULL);

		view->model = GOG_OBJECT (g_value_get_object (value));
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, param_id, pspec);
		return; /* NOTE : RETURN */
	}

	/* renderer set via parent or manually */
	if (init_state && view->renderer != NULL && view->model != NULL) {
		GogViewClass *klass = GOG_VIEW_GET_CLASS (view);
		GSList *ptr = view->model->children;

		for ( ;ptr != NULL ; ptr = ptr->next)
			gog_object_new_view (ptr->data, view);

		g_signal_connect_object (G_OBJECT (view->model),
			"child_added",
			G_CALLBACK (cb_child_added), view, 0);
		g_signal_connect_object (G_OBJECT (view->model),
			"child_removed",
			G_CALLBACK (cb_remove_child), view, 0);
		g_signal_connect_object (G_OBJECT (view->model),
			"changed",
			G_CALLBACK (cb_model_changed), view, 0);

		if (klass->state_init != NULL)
			(klass->state_init) (view);
	}
}

static void
gog_view_finalize (GObject *obj)
{
	GogView *tmp, *view = GOG_VIEW (obj);
	GSList *ptr;

	if (view->parent != NULL)
		view->parent->children = g_slist_remove (view->parent->children, view);

	for (ptr = view->children; ptr != NULL ; ptr = ptr->next) {
		tmp = GOG_VIEW (ptr->data);
		/* not really necessary, but helpful during initial deployment
		 * when not everything has a view yet */
		if (tmp != NULL) {
			tmp->parent = NULL; /* short circuit */
			g_object_unref (tmp);
		}
	}
	g_slist_free (view->children);
	view->children = NULL;

	if (parent_klass != NULL && parent_klass->finalize)
		(*parent_klass->finalize) (obj);
}

static void
gog_view_size_request_real (GogView *view, GogViewRequisition *req)
{
	req->w = req->h = 1.;
}

static void
gog_view_size_allocate_real (GogView *view, GogViewAllocation const *allocation)
{
}

/* A simple default implementation */
static void
gog_view_render_real (GogView *view, GogViewAllocation const *bbox)
{
	GSList *ptr;
	GogView *child;
	GogViewClass *klass;

	for (ptr = view->children ; ptr != NULL ; ptr = ptr->next) {
		child = ptr->data;
		klass = GOG_VIEW_GET_CLASS (child);
		g_return_if_fail (child->renderer != NULL);

#warning TODO clip based on bbox
		(klass->render) (child, bbox);
	}
}

static void
gog_view_class_init (GogViewClass *view_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) view_klass;

	parent_klass = g_type_class_peek_parent (view_klass);
	gobject_klass->set_property = gog_view_set_property;
	gobject_klass->finalize	    = gog_view_finalize;
	view_klass->size_request    = gog_view_size_request_real;
	view_klass->size_allocate   = gog_view_size_allocate_real;
	view_klass->render	    = gog_view_render_real;

	g_object_class_install_property (gobject_klass, GOG_VIEW_PROP_PARENT,
		g_param_spec_object ("parent", "parent",
			"the GogView parent",
			GOG_VIEW_TYPE, G_PARAM_WRITABLE));
	g_object_class_install_property (gobject_klass, GOG_VIEW_PROP_MODEL,
		g_param_spec_object ("model", "model",
			"the GogObject this view displays",
			GOG_OBJECT_TYPE, G_PARAM_WRITABLE));
}

static void
gog_view_init (GogView *view)
{
	view->allocation_valid  = FALSE;
	view->child_allocations_valid = FALSE;
	view->being_updated = FALSE;
	view->model	   = NULL;
	view->parent	   = NULL;
	view->children	   = NULL;
}

GSF_CLASS_ABSTRACT (GogView, gog_view,
		    gog_view_class_init, gog_view_init,
		    G_TYPE_OBJECT)

GogObject *
gog_view_get_model (GogView const *view)
{
	return view->model;
}

/**
 * gog_view_queue_redraw :
 * @view : a #GogView
 *
 * Requests a redraw for the entire graph.
 **/
void
gog_view_queue_redraw (GogView *view)
{
	g_return_if_fail (GOG_VIEW (view) != NULL);
	g_return_if_fail (view->renderer != NULL);

	gog_renderer_request_update (view->renderer);
}

/**
 * gog_view_queue_resize :
 * @view : a #GogView
 *
 * Flags a view to have its size renegotiated; should
 * be called when a model for some reason has a new size request.
 * For example, when you change the size of a legend.
 **/
void
gog_view_queue_resize (GogView *view)
{
	g_return_if_fail (GOG_VIEW (view) != NULL);
	g_return_if_fail (view->renderer != NULL);

	gog_renderer_request_update (view->renderer);

	view->allocation_valid = FALSE; /* in case there is no parent */
	if (NULL == (view = view->parent))
		return;
	view->allocation_valid = FALSE;
	while (NULL != (view = view->parent) && view->child_allocations_valid)
		view->child_allocations_valid = FALSE;
}

/**
 * gog_view_size_request :
 * @view : a #GogView
 * @requisition : a #GogViewRequisition.
 *
 * When called @requisition holds the available space and is populated with the
 * desired size based on that input and other elements of the view or its model's
 * state (eg the position).
 *
 * Remember that the size request is not necessarily the size a view will
 * actually be allocated.
 **/
void
gog_view_size_request (GogView *view, GogViewRequisition *requisition)
{
	GogViewClass *klass = GOG_VIEW_GET_CLASS (view);

	g_return_if_fail (klass != NULL);
	g_return_if_fail (requisition != NULL);
	(klass->size_request) (view, requisition);
}

/**
 * gog_view_size_allocate :
 * @view : a #GogView
 * @allocation: position and size to be allocated to @view
 *
 * Assign a size and position to a GogView.  Primarilly used by containers.
 **/
void
gog_view_size_allocate (GogView *view, GogViewAllocation const *allocation)
{
	GogViewClass *klass = GOG_VIEW_GET_CLASS (view);

	g_return_if_fail (allocation != NULL);
	g_return_if_fail (klass != NULL);
	g_return_if_fail (klass->size_allocate != NULL);
	g_return_if_fail (!view->being_updated);

	g_warning ("size_allocate %s %p : x = %g, y = %g w = %g, h = %g",
		   G_OBJECT_TYPE_NAME (view), view,
		   allocation->x, allocation->y, allocation->w, allocation->h);

	view->being_updated = TRUE;
	(klass->size_allocate) (view, allocation);
	view->being_updated = FALSE;

	if (&view->allocation != allocation)
		view->allocation = *allocation;
	view->allocation_valid = view->child_allocations_valid = TRUE;
}

gboolean
gog_view_update_sizes (GogView *view)
{
	g_return_val_if_fail (GOG_VIEW (view) != NULL, TRUE);
	g_return_val_if_fail (!view->being_updated, TRUE);

	if (!view->allocation_valid)
		gog_view_size_allocate (view, &view->allocation);
	else if (!view->child_allocations_valid) {
		GSList *ptr;

		view->being_updated = TRUE;
		for (ptr = view->children ; ptr != NULL ; ptr = ptr->next)
			gog_view_update_sizes (ptr->data);
		view->being_updated = FALSE;

		view->child_allocations_valid = TRUE;
	} else
		return FALSE;
	return TRUE;
}

void
gog_view_render	(GogView *view, GogViewAllocation const *bbox)
{
	GogViewClass *klass = GOG_VIEW_GET_CLASS (view);
	klass->render (view, bbox);
}

/**
 * gog_view_point :
 * @view : a #GogView
 * @x : 
 * @y :
 *
 * Returns the #GogObject that contains @x,@y.  The caller is responsible for
 * unrefing the result.
 **/
GogObject *
gog_view_point (GogView *view, double x, double y)
{
	GSList *ptr;
	GogObject *res;
	GogViewClass *klass = GOG_VIEW_GET_CLASS (view);

	g_return_val_if_fail (klass != NULL, NULL);
	g_return_val_if_fail (view->allocation_valid, NULL);
	g_return_val_if_fail (view->child_allocations_valid, NULL);

	if (x < view->allocation.x ||
	    x >= (view->allocation.x + view->allocation.w) ||
	    y < view->allocation.y ||
	    y >= (view->allocation.y + view->allocation.h))
		return NULL;

	for (ptr = view->children; ptr != NULL ; ptr = ptr->next) {
		res = gog_view_point (ptr->data, x, y);
		if (res != NULL)
			return res;
	}

	res = (klass->point != NULL) ? (klass->point) (view, x, y) : view->model;
	if (res != NULL)
		g_object_ref (res);
	return res;
}
