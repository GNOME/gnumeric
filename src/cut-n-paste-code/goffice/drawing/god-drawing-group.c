/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */

/*
 * god-drawing-group.c: MS Office Graphic Object support
 *
 * Copyright (C) 2000-2002
 *	Jody Goldberg (jody@gnome.org)
 *	Michael Meeks (mmeeks@gnu.org)
 *      Christopher James Lahey <clahey@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

#include "god-drawing-group.h"
#include <gsf/gsf-impl-utils.h>
#include <string.h>

static GObjectClass *parent_class;

struct GodDrawingGroupPrivate_ {
	gint dummy;
};

static void
god_drawing_group_init (GObject *object)
{
	GodDrawingGroup *group = GOD_DRAWING_GROUP (object);
	group->priv = g_new0 (GodDrawingGroupPrivate, 1);
}

static void
god_drawing_group_finalize (GObject *object)
{
	GodDrawingGroup *group = GOD_DRAWING_GROUP (object);

	g_free (group->priv);
	group->priv = NULL;

	G_OBJECT_CLASS (parent_klass)->finalize (obj);
}

static void
god_drawing_group_class_init (GodDrawingGroupClass *class)
{
	GObjectClass *object_class;

	object_class           = (GObjectClass *) class;

	parent_class           = g_type_class_peek_parent (class);

	object_class->finalize = god_drawing_group_finalize;
}

GSF_CLASS (GodDrawingGroup, god_drawing_group,
	   god_drawing_group_class_init, god_drawing_group_init,
	   G_TYPE_OBJECT)
