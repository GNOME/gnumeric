/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-outlined-object.h : some utility classes for objects with outlines.
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
#ifndef GOG_OUTLINED_OBJECT_H
#define GOG_OUTLINED_OBJECT_H

#include <goffice/graph/goffice-graph.h>
#include <goffice/graph/gog-styled-object.h>
#include <goffice/graph/gog-view.h>

G_BEGIN_DECLS

typedef struct {
	GogStyledObject	base;
	double	 padding_pts;
} GogOutlinedObject;

typedef	GogStyledObjectClass GogOutlinedObjectClass; 

#define GOG_OUTLINED_OBJECT_TYPE  (gog_outlined_object_get_type ())
#define GOG_OUTLINED_OBJECT(o)	  (G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_OUTLINED_OBJECT_TYPE, GogOutlinedObject))
#define IS_GOG_OUTLINED_OBJECT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_OUTLINED_OBJECT_TYPE))

GType  gog_outlined_object_get_type (void);
double gog_outlined_object_get_pad  (GogOutlinedObject const *goo);

/****************************************************************************/

typedef GogView		GogOutlinedView;
typedef struct {
	GogViewClass	base;
	gboolean	call_parent_render;
} GogOutlinedViewClass;

#define GOG_OUTLINED_VIEW_TYPE  	(gog_outlined_view_get_type ())
#define GOG_OUTLINED_VIEW(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_OUTLINED_VIEW_TYPE, GogOutlinedView))
#define IS_GOG_OUTLINED_VIEW(o) 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_OUTLINED_VIEW_TYPE))
#define GOG_OUTLINED_VIEW_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GOG_OUTLINED_VIEW_TYPE, GogOutlinedViewClass))

GType   gog_outlined_view_get_type (void);

G_END_DECLS

#endif /* GOG_OUTLINED_VIEW_H */
