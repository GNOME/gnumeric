/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-renderer.h : An abstract interface for rendering engines
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
#ifndef GOG_RENDERER_H
#define GOG_RENDERER_H

#include <goffice/graph/goffice-graph.h>
#include <glib-object.h>
#include <libart_lgpl/libart.h>

#define GOG_RENDERER_TYPE	  (gog_renderer_get_type ())
#define GOG_RENDERER(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GOG_RENDERER_TYPE, GogRenderer))
#define IS_GOG_RENDERER(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GOG_RENDERER_TYPE))
#define GOG_RENDERER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GOG_RENDERER_TYPE, GogRendererClass))

GType gog_renderer_get_type            (void); 

void   gog_renderer_request_update (GogRenderer *r);
void   gog_renderer_begin_drawing  (GogRenderer *r);
void   gog_renderer_end_drawing    (GogRenderer *r);

void   gog_renderer_push_style     (GogRenderer *r, GogStyle *style);
void   gog_renderer_pop_style      (GogRenderer *r);

void   gog_renderer_draw_path      (GogRenderer *r, ArtVpath *path);
void   gog_renderer_draw_polygon   (GogRenderer *r, ArtVpath *path,
				    gboolean narrow);
void   gog_renderer_draw_rectangle (GogRenderer *r, GogViewAllocation const *rect);

/* measurement */
double gog_renderer_outline_size   (GogRenderer *r, GogStyle *s);
double gog_renderer_pt2r_x   	   (GogRenderer *r, double d);
double gog_renderer_pt2r_y   	   (GogRenderer *r, double d);
double gog_renderer_pt2r   	   (GogRenderer *r, double d);

#endif /* GOG_RENDERER_H */
