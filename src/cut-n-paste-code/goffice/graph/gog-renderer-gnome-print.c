/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-renderer-gnome-print.c :
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
#include <goffice/graph/gog-renderer-gnome-print.h>
#include <goffice/graph/gog-renderer-impl.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-view.h>
#include <goffice/utils/go-color.h>
#include <goffice/utils/go-units.h>

#include <gsf/gsf-impl-utils.h>

#include <math.h>

#define GOG_RENDERER_GNOME_PRINT_TYPE	(gog_renderer_gnome_print_get_type ())
#define GOG_RENDERER_GNOME_PRINT(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_RENDERER_GNOME_PRINT_TYPE, GogRendererGnomePrint))
#define IS_GOG_RENDERER_GNOME_PRINT(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_RENDERER_GNOME_PRINT_TYPE))

typedef struct _GogRendererGnomePrint GogRendererGnomePrint;

struct _GogRendererGnomePrint {
	GogRenderer base;

	GnomePrintContext *gp_context;
	int last_alpha;
};

typedef GogRendererClass GogRendererGnomePrintClass;

static GObjectClass *parent_klass;

static GType gog_renderer_gnome_print_get_type (void);

static void
gog_renderer_gnome_print_finalize (GObject *obj)
{
	GogRendererGnomePrint *prend = GOG_RENDERER_GNOME_PRINT (obj);

	if (prend->gp_context != NULL) {
		g_object_unref (prend->gp_context);
		prend->gp_context = NULL;
	}

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static void
set_color (GogRendererGnomePrint *prend, GOColor color)
{
	int alpha = UINT_RGBA_A (color);
	double r = ((double) UINT_RGBA_R (color)) / 255.;
	double g = ((double) UINT_RGBA_G (color)) / 255.;
	double b = ((double) UINT_RGBA_B (color)) / 255.;
	double a = ((double) UINT_RGBA_A (color)) / 255.;
	gnome_print_setrgbcolor (prend->gp_context, r, g, b);
	if (prend->last_alpha != alpha) {
		gnome_print_setopacity (prend->gp_context, alpha / (double) 0xff);
		prend->last_alpha = alpha;
	}
}

static void
draw_path (GogRendererGnomePrint *prend, ArtVpath *path)
{
	gnome_print_newpath (prend->gp_context);
	for ( ; path->code != ART_END ; path++)
		switch (path->code) {
		case ART_MOVETO :
			gnome_print_moveto (prend->gp_context,
					    path->x, -path->y);
			break;
		case ART_LINETO :
			gnome_print_lineto (prend->gp_context,
					    path->x, -path->y);
			break;
		default :
			break;
		}
}

static void
gog_renderer_gnome_print_draw_path (GogRenderer *renderer, ArtVpath *path)
{
	GogRendererGnomePrint *prend = GOG_RENDERER_GNOME_PRINT (renderer);
	GogStyle *style = renderer->cur_style;

	set_color (prend, style->outline.color);
	gnome_print_setlinewidth (prend->gp_context, 
		gog_renderer_outline_size (renderer, style));
	draw_path (prend, path);
	gnome_print_stroke (prend->gp_context);
}

static void
gog_renderer_gnome_print_draw_polygon (GogRenderer *renderer, ArtVpath *path, gboolean narrow)
{
	GogRendererGnomePrint *prend = GOG_RENDERER_GNOME_PRINT (renderer);
	GogStyle *style = renderer->cur_style;
	gboolean with_outline = (!narrow && style->outline.width >= 0.);

	if (style->fill.type != GOG_FILL_STYLE_NONE || with_outline) {
		draw_path (prend, path);
		gnome_print_closepath (prend->gp_context);
	}

	if (style->fill.type != GOG_FILL_STYLE_NONE) {

		switch (style->fill.type) {
		case GOG_FILL_STYLE_SOLID:
			gnome_print_gsave (prend->gp_context);
			set_color (prend, style->fill.u.solid.color);
			gnome_print_fill (prend->gp_context);
			gnome_print_grestore (prend->gp_context);
			break;

		case GOG_FILL_STYLE_PATTERN:
			g_warning ("unimplemented");
			break;
		case GOG_FILL_STYLE_GRADIENT:
			g_warning ("unimplemented");
			break;

		case GOG_FILL_STYLE_IMAGE:
			g_warning ("unimplemented");
			break;

		case GOG_FILL_STYLE_NONE:
			break; /* impossible */
		}
	}

	if (with_outline) {
		set_color (prend, style->outline.color);
		gnome_print_setlinewidth (prend->gp_context, 
			gog_renderer_outline_size (renderer, style));
		gnome_print_stroke (prend->gp_context);
	}
}

static void
gog_renderer_gnome_print_draw_text (GogRenderer *rend, ArtPoint *pos,
				    char const *text, GogViewRequisition *size)
{
	GogRendererGnomePrint *prend = GOG_RENDERER_GNOME_PRINT (rend);
}

static void
gog_renderer_gnome_print_measure_text (GogRenderer *rend,
				       char const *text, GogViewRequisition *size)
{
}

static void
gog_renderer_gnome_print_class_init (GogRendererClass *rend_klass)
{
	GObjectClass *gobject_klass   = (GObjectClass *) rend_klass;

	parent_klass = g_type_class_peek_parent (rend_klass);
	gobject_klass->finalize	  = gog_renderer_gnome_print_finalize;
	rend_klass->draw_path	  = gog_renderer_gnome_print_draw_path;
	rend_klass->draw_polygon  = gog_renderer_gnome_print_draw_polygon;
	rend_klass->draw_text	  = gog_renderer_gnome_print_draw_text;
	rend_klass->measure_text  = gog_renderer_gnome_print_measure_text;
}

static void
gog_renderer_gnome_print_init (GogRendererGnomePrint *prend)
{
	prend->gp_context = NULL;
	prend->last_alpha = -1;
}

static GSF_CLASS (GogRendererGnomePrint, gog_renderer_gnome_print,
		  gog_renderer_gnome_print_class_init, gog_renderer_gnome_print_init,
		  GOG_RENDERER_TYPE)

void
gog_graph_print_to_gnome_print (GogGraph *graph,
				GnomePrintContext *gp_context,
				double base_x, double base_y,
				double coords[])
{
	GogViewAllocation allocation;
	GogRendererGnomePrint *prend =
		g_object_new (GOG_RENDERER_GNOME_PRINT_TYPE,
			      "model", graph,NULL);
	prend->gp_context = gp_context;

	allocation.x = 0.;
	allocation.y = 0.;
	allocation.w = fabs (coords[2] - coords[0]);
	allocation.h = fabs (coords[3] - coords[1]);
	gnome_print_gsave (gp_context);
	gnome_print_translate (gp_context,
		base_x, base_y);

	gog_view_size_allocate (prend->base.view, &allocation);

	gog_view_render	(prend->base.view, NULL);

	gnome_print_grestore (gp_context);
	g_object_unref (prend);
}
