/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * undo.c:
 *
 * Authors:
  *  Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2010 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "undo.h"
#include <gsf/gsf-impl-utils.h>

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_undo_colrow_restore_state_group_parent_class;

static void
gnm_undo_colrow_restore_state_group_finalize (GObject *o)
{
	GNMUndoColrowRestoreStateGroup *ua = (GNMUndoColrowRestoreStateGroup *)o;

	colrow_state_group_destroy (ua->saved_state);
	ua->saved_state = NULL;
	colrow_index_list_destroy (ua->selection);
	ua->selection = NULL;
}

static void
gnm_undo_colrow_restore_state_group_undo (GOUndo *u, gpointer data)
{
	GNMUndoColrowRestoreStateGroup *ua = (GNMUndoColrowRestoreStateGroup *)u;

	colrow_restore_state_group (ua->sheet, ua->is_cols, ua->selection, ua->saved_state);
}

static void
gnm_undo_colrow_restore_state_group_class_init (GObjectClass *gobject_class)
{
	GOUndoClass *uclass = (GOUndoClass *)gobject_class;

	gnm_undo_colrow_restore_state_group_parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gnm_undo_colrow_restore_state_group_finalize;
	uclass->undo = gnm_undo_colrow_restore_state_group_undo;
}


GSF_CLASS (GNMUndoColrowRestoreStateGroup, gnm_undo_colrow_restore_state_group,
	   gnm_undo_colrow_restore_state_group_class_init, NULL, GO_TYPE_UNDO)

/**
 * gnm_undo_colrow_restore_state_group_new:
 *
 * Returns: a new undo object.
 **/

GOUndo *
gnm_undo_colrow_restore_state_group_new (Sheet *sheet, gboolean is_cols,
					ColRowIndexList *selection,
					ColRowStateGroup *saved_state)
{
	GNMUndoColrowRestoreStateGroup *ua = g_object_new (GNM_TYPE_UNDO_COLROW_RESTORE_STATE_GROUP, NULL);

	ua->sheet = sheet;
	ua->is_cols = is_cols;
	ua->selection = selection;
	ua->saved_state = saved_state;

	return (GOUndo *)ua;
}

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_undo_colrow_set_sizes_parent_class;

static void
gnm_undo_colrow_set_sizes_finalize (GObject *o)
{
	GNMUndoColrowSetSizes *ua = (GNMUndoColrowSetSizes *)o;

	colrow_index_list_destroy (ua->selection);
	ua->selection = NULL;
}

static void
gnm_undo_colrow_set_sizes_undo (GOUndo *u, gpointer data)
{
	GNMUndoColrowSetSizes *ua = (GNMUndoColrowSetSizes *)u;

	colrow_set_sizes (ua->sheet, ua->is_cols, ua->selection, ua->new_size);
}

static void
gnm_undo_colrow_set_sizes_class_init (GObjectClass *gobject_class)
{
	GOUndoClass *uclass = (GOUndoClass *)gobject_class;

	gnm_undo_colrow_set_sizes_parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gnm_undo_colrow_set_sizes_finalize;
	uclass->undo = gnm_undo_colrow_set_sizes_undo;
}


GSF_CLASS (GNMUndoColrowSetSizes, gnm_undo_colrow_set_sizes,
	   gnm_undo_colrow_set_sizes_class_init, NULL, GO_TYPE_UNDO)

/**
 * gnm_undo_colrow_set_sizes_new:
 *
 * Returns: a new undo object.
 **/

GOUndo *
gnm_undo_colrow_set_sizes_new (Sheet *sheet, gboolean is_cols,
			       ColRowIndexList *selection,
			       int new_size)
{
	GNMUndoColrowSetSizes *ua = g_object_new (GNM_TYPE_UNDO_COLROW_SET_SIZES, NULL);

	ua->sheet = sheet;
	ua->is_cols = is_cols;
	ua->selection = selection;
	ua->new_size = new_size;

	return (GOUndo *)ua;
}

/* ------------------------------------------------------------------------- */
