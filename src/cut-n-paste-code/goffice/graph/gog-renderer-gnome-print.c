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

#include <libart_lgpl/art_render_gradient.h>
#include <libart_lgpl/art_render_svp.h>

#include <math.h>
#include <string.h>

#define GOG_RENDERER_GNOME_PRINT_TYPE	(gog_renderer_gnome_print_get_type ())
#define GOG_RENDERER_GNOME_PRINT(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_RENDERER_GNOME_PRINT_TYPE, GogRendererGnomePrint))
#define IS_GOG_RENDERER_GNOME_PRINT(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_RENDERER_GNOME_PRINT_TYPE))

typedef struct _GogRendererGnomePrint GogRendererGnomePrint;

struct _GogRendererGnomePrint {
	GogRenderer base;

	GnomePrintContext *gp_context;
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
	double r = ((double) UINT_RGBA_R (color)) / 255.;
	double g = ((double) UINT_RGBA_G (color)) / 255.;
	double b = ((double) UINT_RGBA_B (color)) / 255.;
	double a = ((double) UINT_RGBA_A (color)) / 255.;
	gnome_print_setrgbcolor (prend->gp_context, r, g, b);
	gnome_print_setopacity (prend->gp_context, a);
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

	set_color (prend, style->line.color);
	gnome_print_setlinewidth (prend->gp_context, 
		gog_renderer_line_size (renderer, style->line.width));
	draw_path (prend, path);
	gnome_print_stroke (prend->gp_context);
}

#define PIXBUF_SIZE 1024
static void
gog_renderer_gnome_print_draw_polygon (GogRenderer *renderer, ArtVpath *path, gboolean narrow)
{
	GogRendererGnomePrint *prend = GOG_RENDERER_GNOME_PRINT (renderer);
	GogStyle *style = renderer->cur_style;
	gboolean with_outline = (!narrow && style->outline.width >= 0.);
	GdkPixbuf *image;
	GError *err = NULL;
	ArtDRect bbox;
	ArtRender *render;
	gint i, j, imax, jmax, w, h, x, y;
	GOColor color;
	ArtGradientLinear gradient;
	ArtGradientStop stops[] = {
		{ 0., { 0, 0, 0, 0 }},
		{ 1., { 0, 0, 0, 0 }}
	};

	if (style->fill.type != GOG_FILL_STYLE_NONE || with_outline) {
		draw_path (prend, path);
		gnome_print_closepath (prend->gp_context);
	}

	if (style->fill.type != GOG_FILL_STYLE_NONE) {

		art_vpath_bbox_drect (path, &bbox);

		switch (style->fill.type) {
		case GOG_FILL_STYLE_PATTERN:
			gnome_print_gsave (prend->gp_context);
			if (go_pattern_is_solid (&style->fill.u.pattern.pat, &color)) {
				set_color (prend, color);
				gnome_print_fill (prend->gp_context);
				gnome_print_grestore (prend->gp_context);
			} else {
				ArtSVP *fill = art_svp_from_vpath (path);
				image = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, bbox.x1, bbox.y1);
				gdk_pixbuf_fill (image, 0);
				go_pattern_render_svp (&style->fill.u.pattern.pat,
					fill, 0, 0, bbox.x1, bbox.y1,
					gdk_pixbuf_get_pixels (image),
					gdk_pixbuf_get_rowstride (image));

				gnome_print_translate (prend->gp_context, 0, - bbox.y1);
				gnome_print_scale (prend->gp_context, bbox.x1, bbox.y1);
				gnome_print_rgbaimage (prend->gp_context,
					gdk_pixbuf_get_pixels(image),
					gdk_pixbuf_get_width(image),
					gdk_pixbuf_get_height(image),
					gdk_pixbuf_get_rowstride(image));

				art_free (fill);
				g_object_unref (image);
			}
			gnome_print_grestore (prend->gp_context);
			break;

		case GOG_FILL_STYLE_GRADIENT:
			image = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, PIXBUF_SIZE, PIXBUF_SIZE);
			gnome_print_gsave (prend->gp_context);
			gnome_print_clip (prend->gp_context);
			render = art_render_new (0, 0, PIXBUF_SIZE, PIXBUF_SIZE,
				gdk_pixbuf_get_pixels (image), 
				gdk_pixbuf_get_rowstride (image),
				gdk_pixbuf_get_n_channels (image) - 1,
				8, ART_ALPHA_SEPARATE, NULL);
			if (style->fill.u.gradient.dir < 4) {
				gradient. a = 0.;
				gradient. b = 1. / (PIXBUF_SIZE - 1);
				gradient. c = 0.;
			} else if (style->fill.u.gradient.dir < 8) {
				gradient. a = 1. / (PIXBUF_SIZE - 1);
				gradient. b = 0.;
				gradient. c = 0.;
			} else if (style->fill.u.gradient.dir < 12) {
				gradient. a = 1. / (2 * PIXBUF_SIZE - 1);
				gradient. b = 1. / (2 * PIXBUF_SIZE - 1);
				gradient. c = 0.;
			} else {
				gradient. a = 1. / (2 * PIXBUF_SIZE - 1);
				gradient. b = -1. / (2 * PIXBUF_SIZE - 1);
				gradient. c = .5;
			}

			switch (style->fill.u.gradient.dir % 4) {
			case 0:
				gradient.spread = ART_GRADIENT_REPEAT;
				gradient.n_stops = G_N_ELEMENTS (stops);
				gradient.stops = stops;
				go_color_to_artpix (stops[0].color,
							style->fill.u.gradient.start);
				go_color_to_artpix (stops[1].color,
							style->fill.u.gradient.end);
				break;
			case 1:
				gradient.spread = ART_GRADIENT_REPEAT;
				gradient.n_stops = G_N_ELEMENTS (stops);
				gradient.stops = stops;
				go_color_to_artpix (stops[0].color,
							style->fill.u.gradient.end);
				go_color_to_artpix (stops[1].color,
							style->fill.u.gradient.start);
				break;
			case 2:
				gradient.spread = ART_GRADIENT_REFLECT;
				gradient.n_stops = G_N_ELEMENTS (stops);
				gradient.stops = stops;
				go_color_to_artpix (stops[0].color,
							style->fill.u.gradient.start);
				go_color_to_artpix (stops[1].color,
							style->fill.u.gradient.end);
				gradient.a *= 2;
				gradient.b *= 2;
				gradient.c *= 2;
				break;
			case 3:
				gradient.spread = ART_GRADIENT_REFLECT;
				gradient.n_stops = G_N_ELEMENTS (stops);
				gradient.stops = stops;
				go_color_to_artpix (stops[0].color,
							style->fill.u.gradient.end);
				go_color_to_artpix (stops[1].color,
							style->fill.u.gradient.start);
				gradient.a *= 2;
				gradient.b *= 2;
				gradient.c *= 2;
				break;
			}
			art_render_gradient_linear (render,
				&gradient, ART_FILTER_NEAREST);
			art_render_invoke (render);
			gnome_print_translate (prend->gp_context, bbox.x0, - bbox.y1);
			gnome_print_scale (prend->gp_context, bbox.x1 - bbox.x0, bbox.y1 - bbox.y0);
			gnome_print_rgbaimage (prend->gp_context, gdk_pixbuf_get_pixels(image),
									gdk_pixbuf_get_width(image),
									gdk_pixbuf_get_height(image),
									gdk_pixbuf_get_rowstride(image));
			gnome_print_grestore (prend->gp_context);
			g_object_unref (image);
			break;

		case GOG_FILL_STYLE_IMAGE:
			if (!style->fill.u.image.image_file)
				break;
			image = gdk_pixbuf_new_from_file (style->fill.u.image.image_file, &err);
			if (err != NULL)
				break;
			gnome_print_gsave (prend->gp_context);
			gnome_print_clip (prend->gp_context);
			switch (style->fill.u.image.type) {
			case GOG_IMAGE_STRETCHED:
				gnome_print_translate (prend->gp_context, bbox.x0, - bbox.y1);
				gnome_print_scale (prend->gp_context, bbox.x1 - bbox.x0, bbox.y1 - bbox.y0);
				if (gdk_pixbuf_get_has_alpha (image))
					gnome_print_rgbaimage (prend->gp_context, gdk_pixbuf_get_pixels(image),
											gdk_pixbuf_get_width(image),
											gdk_pixbuf_get_height(image),
											gdk_pixbuf_get_rowstride(image));
				else
					gnome_print_rgbimage (prend->gp_context, gdk_pixbuf_get_pixels(image),
											gdk_pixbuf_get_width(image),
											gdk_pixbuf_get_height(image),
											gdk_pixbuf_get_rowstride(image));
				break;

			case GOG_IMAGE_WALLPAPER:
				imax = (bbox.x1 - bbox.x0) / (w = gdk_pixbuf_get_width (image));
				jmax = (bbox.y1 - bbox.y0) / (h = gdk_pixbuf_get_height (image));
				x = 0;
				for (i = 0; i < imax; i++) {
					y = 0;
					for (j = 0; j < jmax; j++) {
						gnome_print_gsave (prend->gp_context);
						gnome_print_translate (prend->gp_context, bbox.x0 + x, - y - h - bbox.y0);
						gnome_print_scale (prend->gp_context, w, h);
						if (gdk_pixbuf_get_has_alpha (image))
							gnome_print_rgbaimage (prend->gp_context, gdk_pixbuf_get_pixels(image),
													w, h,
													gdk_pixbuf_get_rowstride(image));
						else
							gnome_print_rgbimage (prend->gp_context, gdk_pixbuf_get_pixels(image),
													w, h,
													gdk_pixbuf_get_rowstride(image));
						gnome_print_grestore (prend->gp_context);
						y += h;
					}
					gnome_print_gsave (prend->gp_context);
					gnome_print_translate (prend->gp_context, bbox.x0 + x, - y - (int)(bbox.y1 - bbox.y0) % h - bbox.y0);
					gnome_print_scale (prend->gp_context, w, (int)(bbox.y1 - bbox.y0) % h);
					if (gdk_pixbuf_get_has_alpha (image))
						gnome_print_rgbaimage (prend->gp_context, gdk_pixbuf_get_pixels(image),
												w, (int)(bbox.y1 - bbox.y0) % h,
												gdk_pixbuf_get_rowstride(image));
					else
						gnome_print_rgbimage (prend->gp_context, gdk_pixbuf_get_pixels(image),
												w, (int)(bbox.y1 - bbox.y0) % h,
												gdk_pixbuf_get_rowstride(image));
						gnome_print_grestore (prend->gp_context);
					x += w;
				}
				y = 0;
				for (j = 0; j < jmax; j++) {
					gnome_print_gsave (prend->gp_context);
					gnome_print_translate (prend->gp_context, bbox.x0 + x, - y - h - bbox.y0);
					gnome_print_scale (prend->gp_context, (int)(bbox.x1 - bbox.x0) % w, h);
					if (gdk_pixbuf_get_has_alpha (image))
						gnome_print_rgbaimage (prend->gp_context, gdk_pixbuf_get_pixels(image),
												(int)(bbox.x1 - bbox.x0) % w, h,
												gdk_pixbuf_get_rowstride(image));
					else
						gnome_print_rgbimage (prend->gp_context, gdk_pixbuf_get_pixels(image),
												(int)(bbox.x1 - bbox.x0) % w, h,
												gdk_pixbuf_get_rowstride(image));
					gnome_print_grestore (prend->gp_context);
					y += h;
				}
				gnome_print_gsave (prend->gp_context);
				gnome_print_translate (prend->gp_context, bbox.x0 + x, - y - (int)(bbox.y1 - bbox.y0) % h - bbox.y0);
				gnome_print_scale (prend->gp_context, (int)(bbox.x1 - bbox.x0) % w, (int)(bbox.y1 - bbox.y0) % h);
				if (gdk_pixbuf_get_has_alpha (image))
					gnome_print_rgbaimage (prend->gp_context, gdk_pixbuf_get_pixels(image),
											(int)(bbox.x1 - bbox.x0) % w, (int)(bbox.y1 - bbox.y0) % h,
											gdk_pixbuf_get_rowstride(image));
				else
					gnome_print_rgbimage (prend->gp_context, gdk_pixbuf_get_pixels(image),
											(int)(bbox.x1 - bbox.x0) % w, (int)(bbox.y1 - bbox.y0) % h,
											gdk_pixbuf_get_rowstride(image));
					gnome_print_grestore (prend->gp_context);
				break;
			}
			gnome_print_grestore (prend->gp_context);
			g_object_unref (image);
			break;

		case GOG_FILL_STYLE_NONE:
			break; /* impossible */
		}
	}

	if (with_outline) {
		set_color (prend, style->outline.color);
		gnome_print_setlinewidth (prend->gp_context, 
			gog_renderer_line_size (renderer, style->outline.width));
		gnome_print_stroke (prend->gp_context);
	}
}

static void
gog_renderer_gnome_print_draw_text (GogRenderer *rend, ArtPoint *pos,
				    char const *text, GogViewRequisition *size)
{
	GogRendererGnomePrint *prend = GOG_RENDERER_GNOME_PRINT (rend);
#warning FIXME need to take descent into account
	gnome_print_moveto (prend->gp_context, pos->x, -pos->y);
	gnome_print_show (prend->gp_context, text);
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
}

static GSF_CLASS (GogRendererGnomePrint, gog_renderer_gnome_print,
		  gog_renderer_gnome_print_class_init, gog_renderer_gnome_print_init,
		  GOG_RENDERER_TYPE)

void
gog_graph_print_to_gnome_print (GogGraph *graph,
				GnomePrintContext *gp_context,
				double width, double height)
{
	GogViewAllocation allocation;
	GogRendererGnomePrint *prend =
		g_object_new (GOG_RENDERER_GNOME_PRINT_TYPE,
			      "model", graph, NULL);
	g_object_ref (gp_context);
	prend->gp_context = gp_context;
	allocation.x = 0.;
	allocation.y = 0.;
	allocation.w = width;
	allocation.h = height;
	gog_view_size_allocate (prend->base.view, &allocation);
	gog_view_render	(prend->base.view, NULL);
	g_object_unref (prend);
}
