/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-object.c :
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
#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-graph-impl.h> /* for gog_graph_request_update */

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
#include <string.h>
#include <stdlib.h>

enum {
	CHILD_ADDED,
	CHILD_REMOVED,
	NAME_CHANGED,
	CHANGED,
	LAST_SIGNAL
};
static gulong gog_object_signals [LAST_SIGNAL] = { 0, };

static GObjectClass *parent_klass;
static void
gog_object_finalize (GObject *gobj)
{
	GogObject *obj = GOG_OBJECT (gobj);

	g_free (obj->name); obj->name = NULL;

	g_slist_foreach (obj->children, (GFunc) g_object_unref, NULL);
	g_slist_free (obj->children);
	obj->children = NULL;

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (gobj);
}

static void
gog_object_parent_changed (GogObject *child, gboolean was_set)
{
	GSList *ptr = child->children;
	for (; ptr != NULL ; ptr = ptr->next) {
		GogObjectClass *klass = GOG_OBJECT_GET_CLASS (ptr->data);
		(*klass->parent_changed) (ptr->data, was_set);
	}
}

static void
gog_object_class_init (GObjectClass *klass)
{
	GogObjectClass *gog_klass = (GogObjectClass *)klass;
	parent_klass = g_type_class_peek_parent (klass);
	klass->finalize = gog_object_finalize;
	gog_klass->parent_changed = gog_object_parent_changed;

	gog_object_signals [CHILD_ADDED] = g_signal_new ("child_added",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GogObjectClass, child_added),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE,
		1, G_TYPE_OBJECT);

	gog_object_signals [CHILD_REMOVED] = g_signal_new ("child_removed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GogObjectClass, child_removed),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE,
		1, G_TYPE_OBJECT);

	gog_object_signals [NAME_CHANGED] = g_signal_new ("name_changed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GogObjectClass, name_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	gog_object_signals [CHANGED] = g_signal_new ("changed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GogObjectClass, changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__BOOLEAN,
		G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
gog_object_init (GogObject *obj)
{
	obj->children = NULL;
	obj->name = NULL;
	obj->needs_update = FALSE;
	obj->being_updated = FALSE;
}

GSF_CLASS (GogObject, gog_object,
	   gog_object_class_init, gog_object_init,
	   G_TYPE_OBJECT)

static char *
gog_object_generate_name (GogObject *obj)
{
	GogObjectClass *klass = GOG_OBJECT_GET_CLASS (obj);
	GogObject *tmp;
	char const *type_name;
	unsigned name_len, i, max_index = 0;
	GSList *ptr;

	g_return_val_if_fail (klass != NULL, NULL);
	g_return_val_if_fail (klass->type_name != NULL, NULL);

	type_name = (*klass->type_name) (obj);

	g_return_val_if_fail (type_name != NULL, NULL);
	name_len = strlen (type_name);

	for (ptr = obj->parent->children; ptr != NULL ; ptr = ptr->next) {
		tmp = GOG_OBJECT (ptr->data);
		if (tmp->name != NULL &&
		    0 == strncmp (type_name, tmp->name, name_len)) {
			i = strtol (tmp->name+name_len, NULL, 10);
			if (max_index < i)
				max_index = i;
		}
	}
	return g_strdup_printf ("%s%d", type_name, max_index + 1);
}

/**
 * gog_object_get_parent :
 * @obj : a #GogObject
 *
 * Returns @obj's parent, potentially NULL if it has not been added to a
 * heirarchy yet.  does not change ref-count in any way.
 **/
GogObject *
gog_object_get_parent (GogObject const *obj)
{
	g_return_val_if_fail (GOG_OBJECT (obj) != NULL, NULL);
	return obj->parent;
}

/**
 * gog_object_get_parent_typed :
 * @obj : a #GogObject
 * @type : a #GType
 *
 * Returns @obj's parent of type @type, potentially NULL if it has not been
 * added to a heirarchy yet or none of the parents are of type @type.
 **/
GogObject *
gog_object_get_parent_typed (GogObject const *obj, GType t)
{
	g_return_val_if_fail (GOG_OBJECT (obj) != NULL, NULL);

	for (; obj != NULL ; obj = obj->parent)
		if (G_TYPE_CHECK_INSTANCE_TYPE (obj, t))
			return GOG_OBJECT (obj); /* const cast */
	return NULL;
}

/**
 * gog_object_get_graph :
 * @obj : const * #GogObject
 *
 * Returns the parent graph.
 **/
GogGraph *
gog_object_get_graph (GogObject const *obj)
{
	g_return_val_if_fail (GOG_OBJECT (obj) != NULL, NULL);

	for (; obj != NULL ; obj = obj->parent)
		if (IS_GOG_GRAPH (obj))
			return GOG_GRAPH (obj);
	return NULL;
}

GogTheme *
gog_object_get_theme (GogObject const *obj)
{
	GogGraph *graph = gog_object_get_graph (obj);

	return (graph != NULL) ? gog_graph_get_theme (graph) : NULL;
}

/**
 * gog_object_get_name :
 * @obj : a #GogObject
 *
 * No need to free the result
 **/
char const *
gog_object_get_name (GogObject const *obj)
{
	g_return_val_if_fail (GOG_OBJECT (obj) != NULL, NULL);
	return obj->name;
}

/**
 * gog_object_set_name :
 * @obj : #GogObject
 * @name :
 * @err : #GError
 *
 * Assign the new name and signals that it has changed.
 * NOTE : it _absorbs_ @name rather than copying it, and generates a new name
 * if @name == NULL
 **/
void
gog_object_set_name (GogObject *obj, char *name, GError **err)
{
	g_return_if_fail (GOG_OBJECT (obj) != NULL);

	if (obj->name == name)
		return;
	g_free (obj->name);
	obj->name = (name != NULL) ? name : gog_object_generate_name (obj);

	g_signal_emit (G_OBJECT (obj),
		gog_object_signals [NAME_CHANGED], 0);
}

/**
 * gog_object_get_children :
 * @obj : a #GogObject
 *
 * The list needs to be Freed
 **/
GSList *
gog_object_get_children (GogObject const *obj)
{
	g_return_val_if_fail (GOG_OBJECT (obj) != NULL, NULL);
	return g_slist_copy (obj->children);
}

/**
 * gog_object_is_deletable :
 * @obj : a #GogObject
 *
 * Can the specified @obj be deleted ?
 **/
gboolean
gog_object_is_deletable (GogObject const *obj)
{
	g_return_val_if_fail (GOG_OBJECT (obj) != NULL, FALSE);

	if (IS_GOG_GRAPH (obj))
		return FALSE;

	return obj->role == NULL || obj->role->can_remove == NULL ||
		(obj->role->can_remove) (obj);
}

struct possible_add_closure {
	GSList *res;
	GogObject const *parent;
};

static void
cb_collect_possible_additions (char const *name, GogObjectRole const *role,
			       struct possible_add_closure *data)
{
	if (role->can_add == NULL || (role->can_add) (data->parent))
		data->res = g_slist_prepend (data->res, (gpointer)role);
}

/**
 * gog_object_possible_additions :
 * @parent : a #GogObject
 *
 * returns a list of GogObjectRoles that could be added
 *
 * The resulting list needs to be freed
 **/
GSList *
gog_object_possible_additions (GogObject const *parent)
{
	GogObjectClass *klass = GOG_OBJECT_GET_CLASS (parent);
	g_return_val_if_fail (klass != NULL, NULL);

	if (klass->roles != NULL) {
		struct possible_add_closure data;
		data.res = NULL;
		data.parent = parent;

		g_hash_table_foreach (klass->roles,
			(GHFunc) cb_collect_possible_additions, &data);

		return data.res;
	}

	return NULL;
}

/**
 * gog_object_get_editor :
 * @obj   : #GogObject
 * @dalloc : #GogDataAllocator
 * @cc     : #CommandContext
 *
 **/
gpointer
gog_object_get_editor (GogObject *obj,
			  GogDataAllocator *dalloc, CommandContext *cc)
{
	GogObjectClass *klass = GOG_OBJECT_GET_CLASS (obj);
	g_return_val_if_fail (klass != NULL, NULL);
	if (klass->editor)
		return (*klass->editor) (obj, dalloc, cc);
	return NULL;
}

/**
 * gog_object_new_view :
 * @obj : a #GogObject
 * @data :
 **/
GogView *
gog_object_new_view (GogObject const *obj, GogView *parent)
{
	GogObjectClass *klass = GOG_OBJECT_GET_CLASS (obj);

	g_return_val_if_fail (klass != NULL, NULL);

	if (klass->view_type != 0)
		/* set model before parent */
		return g_object_new (klass->view_type,
			"model", obj,
			"parent", parent,
			NULL);

	return NULL;
}

void
gog_object_update (GogObject *obj)
{
	GogObjectClass *klass = GOG_OBJECT_GET_CLASS (obj);
	GSList *ptr;

	g_return_if_fail (klass != NULL);

	ptr = obj->children; /* depth first */
	for (; ptr != NULL ; ptr = ptr->next)
		gog_object_update (ptr->data);

	if (obj->needs_update) {
		obj->needs_update = FALSE;
		obj->being_updated = TRUE;
		g_warning ("updating %s (%p)", G_OBJECT_TYPE_NAME (obj), obj);
		if (klass->update != NULL)
			(*klass->update) (obj);
		obj->being_updated = FALSE;
	}
}

gboolean
gog_object_request_update (GogObject *obj)
{
	GogGraph *graph;
	g_return_val_if_fail (GOG_OBJECT (obj), FALSE);
	g_return_val_if_fail (!obj->being_updated, FALSE);

	if (obj->needs_update)
		return FALSE;

	graph = gog_object_get_graph (obj);
	if (graph == NULL) /* we are not linked into a graph yet */
		return FALSE;

	gog_graph_request_update (graph);
	obj->needs_update = TRUE;

	return TRUE;
}

void
gog_object_emit_changed (GogObject *obj, gboolean resize)
{
	g_return_if_fail (GOG_OBJECT (obj));

	g_signal_emit (G_OBJECT (obj),
		gog_object_signals [CHANGED], 0, resize);
}

/******************************************************************************/

/**
 * gog_object_clear_parent :
 * @obj : #GogObject
 *
 * Does _not_ unref the child, which in effect adds a ref by freeing up the ref
 * previously associated with the parent.
 **/
gboolean
gog_object_clear_parent (GogObject *obj)
{
	GogObjectClass *klass = GOG_OBJECT_GET_CLASS (obj);
	GogObject *parent;

	g_return_val_if_fail (GOG_OBJECT (obj), FALSE);
	g_return_val_if_fail (obj->parent != NULL, FALSE);
	g_return_val_if_fail (gog_object_is_deletable (obj), FALSE);

	parent = obj->parent;
	g_signal_emit (G_OBJECT (parent),
		gog_object_signals [CHILD_REMOVED], 0, obj);
	(*klass->parent_changed) (obj, FALSE);

	if (obj->role != NULL && obj->role->pre_remove != NULL)
		(obj->role->pre_remove) (parent, obj);

	parent->children = g_slist_remove (parent->children, obj);
	obj->parent = NULL;

	if (obj->role != NULL && obj->role->post_remove != NULL)
		(obj->role->post_remove) (parent, obj);

	obj->role = NULL;

	return TRUE;
}

static int
order_positions (GogObjectPosition pos)
{
	if (pos & GOG_POSITION_COMPASS)
		return 0;

	if (pos == GOG_POSITION_FILL)
		return 2;
	if (pos == GOG_POSITION_SPECIAL)
		return 3;
	/* GOG_POSITION_MANUAL */
	return 1;
}

/**
 * gog_object_set_parent :
 * @child  : #GogObject.
 * @parent : #GogObject.
 * @name : optionally %NULL.
 * @role : a static string that can be sent to @parent::add
 *
 * Absorbs a ref to @child
 **/
gboolean
gog_object_set_parent (GogObject *child, GogObject *parent,
		       GogObjectRole const *role, char *name)
{
	GogObjectClass *klass = GOG_OBJECT_GET_CLASS (child);
	GSList **step;
	int order;

	g_return_val_if_fail (GOG_OBJECT (child), FALSE);
	g_return_val_if_fail (child->parent == NULL, FALSE);
	g_return_val_if_fail (role != NULL, FALSE);

	child->parent	= parent;
	child->role	= role;
	child->position = role->default_position;

	/* Insert sorted based on hokey little ordering */
	order = order_positions (child->position);
	step = &parent->children;
	while (*step != NULL &&
	       order >= order_positions (GOG_OBJECT ((*step)->data)->position))
		step = &((*step)->next);
	*step = g_slist_prepend (*step, child);

	if (child->name != NULL)
		g_free (child->name);
	child->name = (name != NULL) ? name : gog_object_generate_name (child);

	g_signal_emit (G_OBJECT (parent),
		gog_object_signals [CHILD_ADDED], 0, child);
	(*klass->parent_changed) (child, TRUE);

	return TRUE;
}

GogObject *
gog_object_add_by_role (GogObject *parent, GogObjectRole const *role, GogObject *child)
{
	GType is_a;

	g_return_val_if_fail (role != NULL, NULL);
	g_return_val_if_fail (GOG_OBJECT (parent) != NULL, NULL);

	is_a = g_type_from_name (role->is_a_typename);

	g_return_val_if_fail (is_a != 0, NULL);

	if (child == NULL)
		child = (role->allocate)
			? (role->allocate) (parent)
			: g_object_new (is_a, NULL);

	g_return_val_if_fail (G_TYPE_CHECK_INSTANCE_TYPE (child, is_a), NULL);
	if (gog_object_set_parent (child, parent, role, NULL)) {
		if (role->post_add != NULL)
			(role->post_add) (parent, child);
		return child;
	}
	g_object_unref (child);
	return NULL;
}

GogObject *
gog_object_add_by_name (GogObject *parent,
			char const *role, GogObject *child)
{
	return gog_object_add_by_role (parent,
		gog_object_find_role_by_name (parent, role), child);
}

GogObjectRole const *
gog_object_find_role_by_name (GogObject const *obj, char const *role)
{
	GogObjectClass *klass = GOG_OBJECT_GET_CLASS (obj);

	g_return_val_if_fail (klass != NULL, NULL);

	return g_hash_table_lookup (klass->roles, role);
}

void
gog_object_register_roles (GogObjectClass *klass,
			   GogObjectRole const *roles, unsigned n_roles)
{
	unsigned i;
	if (klass->roles == NULL)
		klass->roles = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0 ; i < n_roles ; i++) {
		g_return_if_fail (g_hash_table_lookup (klass->roles,
			(gpointer )roles[i].id) == NULL);
		g_hash_table_replace (klass->roles,
			(gpointer )roles[i].id, (gpointer) (roles + i));
	}
}
