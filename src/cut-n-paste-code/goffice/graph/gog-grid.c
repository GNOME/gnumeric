/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-grid.c
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
#include <goffice/graph/gog-grid.h>
#include <goffice/graph/gog-styled-object.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-theme.h>
#include <goffice/graph/gog-renderer.h>

#include <src/gui-util.h>
#include <glib/gi18n.h>

#include <gsf/gsf-impl-utils.h>

struct _GogGrid {
	GogStyledObject	base;
};
typedef GogStyledObjectClass GogGridClass;


static GType gog_grid_view_get_type (void);
static GogViewClass *gview_parent_klass;

static void
gog_grid_init_style (GogStyledObject *gso, GogStyle *style)
{
	style->interesting_fields = GOG_STYLE_FILL | GOG_STYLE_LINE;
	gog_theme_init_style (gog_object_get_theme (GOG_OBJECT (gso)),
		style, GOG_OBJECT (gso), 0);
}

static void
gog_grid_class_init (GogGridClass *klass)
{
	GogObjectClass *gog_klass = (GogObjectClass *) klass;
	GogStyledObjectClass *style_klass = (GogStyledObjectClass *) klass;

	gog_klass->view_type	= gog_grid_view_get_type ();
	style_klass->init_style = gog_grid_init_style;
}

GSF_CLASS (GogGrid, gog_grid,
	   gog_grid_class_init, NULL,
	   GOG_STYLED_OBJECT_TYPE)

/************************************************************************/

typedef GogView		GogGridView;
typedef GogViewClass	GogGridViewClass;

#define GOG_GRID_VIEW_TYPE	(gog_grid_view_get_type ())
#define GOG_GRID_VIEW(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_GRID_VIEW_TYPE, GogGridView))
#define IS_GOG_GRID_VIEW(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_GRID_VIEW_TYPE))

static void
gog_grid_view_render (GogView *view, GogViewAllocation const *bbox)
{
	GogGrid *grid = GOG_GRID (view->model);

#warning KLUDGE KLUDGE
	/* there seems to be a fence post discrepency in the code where axis
	 * and plots extend 1 pixel past the edge of the grid.  cheat for now
	 * and extend the grid 1 pixel,  chase this down after the release */
	GogViewAllocation kludge;

	kludge.x = floor (view->allocation.x + .5) - .5;
	kludge.y = floor (view->allocation.y + .5) - .5;
	kludge.h = floor (view->allocation.h + .5) + 2.;
	kludge.w = floor (view->allocation.w + .5) + 2.;

	gog_renderer_push_style (view->renderer, grid->base.style);
	gog_renderer_draw_rectangle (view->renderer, &kludge, NULL);
	gog_renderer_pop_style (view->renderer);
	(gview_parent_klass->render) (view, bbox);
}

static void
gog_grid_view_class_init (GogGridViewClass *gview_klass)
{
	GogViewClass *view_klass    = (GogViewClass *) gview_klass;

	gview_parent_klass = g_type_class_peek_parent (gview_klass);
	view_klass->render = gog_grid_view_render;
}

static GSF_CLASS (GogGridView, gog_grid_view,
	   gog_grid_view_class_init, NULL,
	   GOG_VIEW_TYPE)
