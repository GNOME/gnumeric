/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-renderer-pixbuf.c :
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
#include <goffice/graph/gog-renderer-pixbuf.h>
#include <goffice/graph/gog-renderer-impl.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-view.h>
#include <goffice/utils/go-color.h>

#include <libart_lgpl/art_render_gradient.h>
#include <libart_lgpl/art_render_svp.h>
#include "art_rgba_svp.h"
#include <gsf/gsf-impl-utils.h>

struct _GogRendererPixbuf {
	GogRenderer base;

	int w, h;
	GdkPixbuf *buffer;
	guchar    *pixels; /* from pixbuf */
	int	   rowstride;
};

typedef struct {
	GogRendererClass base;
} GogRendererPixbufClass;

static GObjectClass *parent_klass;

static void
gog_renderer_pixbuf_finalize (GObject *obj)
{
	GogRendererPixbuf *prend = GOG_RENDERER_PIXBUF (obj);

	if (prend->buffer != NULL) {
		g_object_unref (prend->buffer);
		prend->buffer = NULL;
	}

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static void
gog_renderer_pixbuf_begin_drawing (GogRenderer *renderer)
{
	GogRendererPixbuf *prend = GOG_RENDERER_PIXBUF (renderer);

	if (prend->buffer == NULL) {
		prend->buffer = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
						prend->w, prend->h);
		prend->pixels    = gdk_pixbuf_get_pixels (prend->buffer);
		prend->rowstride = gdk_pixbuf_get_rowstride (prend->buffer);
	}
	gdk_pixbuf_fill (prend->buffer, 0);
}

static void
gog_renderer_pixbuf_draw_path (GogRenderer *renderer, ArtVpath *path)
{
	GogRendererPixbuf *prend = GOG_RENDERER_PIXBUF (renderer);
	GogStyle *style = renderer->cur_style;
	double width = gog_renderer_outline_size (renderer, style);
	ArtSVP *svp = art_svp_vpath_stroke (path,
		ART_PATH_STROKE_JOIN_MITER, ART_PATH_STROKE_CAP_SQUARE,
		width, 4, 0.5);
	art_rgba_svp_alpha (svp,
		0, 0, prend->w, prend->h,
		style->outline.color,
		prend->pixels, prend->rowstride,
		NULL);
	art_svp_free (svp);
}

static void
go_color_to_artpix (ArtPixMaxDepth *res, GOColor rgba)
{
	guint8 r = UINT_RGBA_R (rgba);
	guint8 g = UINT_RGBA_G (rgba);
	guint8 b = UINT_RGBA_B (rgba);
	guint8 a = UINT_RGBA_A (rgba);
	res[0] = ART_PIX_MAX_FROM_8 (r);
	res[1] = ART_PIX_MAX_FROM_8 (g);
	res[2] = ART_PIX_MAX_FROM_8 (b);
	res[3] = ART_PIX_MAX_FROM_8 (a);
}

static void
gog_renderer_pixbuf_draw_polygon (GogRenderer *renderer, ArtVpath *path, gboolean narrow)
{
	GogRendererPixbuf *prend = GOG_RENDERER_PIXBUF (renderer);
	GogStyle *style = renderer->cur_style;
	ArtRender *render;
	ArtSVP *fill, *outline = NULL;
	ArtDRect bbox;
	ArtGradientLinear gradient;
	ArtGradientStop stops[] = {
		{ 0., { 0, 0, 0, 0 }},
		{ 1., { 0, 0, 0, 0 }}
	};

	if (!narrow && style->outline.width >= 0.)
		outline = art_svp_vpath_stroke (path,
			ART_PATH_STROKE_JOIN_MITER, ART_PATH_STROKE_CAP_SQUARE,
			gog_renderer_outline_size (renderer, style), 4, 0.5);

	if (style->fill.type != GOG_FILL_STYLE_NONE) {
		fill = art_svp_from_vpath (path);
#if 0 /* art_svp_minus is not implemented */
		if (outline != NULL) {
			ArtSVP *tmp = art_svp_minus (fill, outline);
			art_svp_free (fill);
			fill = tmp;
		}
#endif

		switch (style->fill.type) {
		case GOG_FILL_STYLE_SOLID:
			art_rgba_svp_alpha (fill, 0, 0, prend->w, prend->h,
				style->fill.u.solid.color,
				prend->pixels, prend->rowstride, NULL);
			break;

		case GOG_FILL_STYLE_PATTERN:
			g_warning ("unimplemented");
			break;
		case GOG_FILL_STYLE_GRADIENT:
			art_vpath_bbox_drect (path, &bbox);

			render = art_render_new (0, 0, prend->w, prend->h,
				prend->pixels, prend->rowstride,
				gdk_pixbuf_get_n_channels (prend->buffer) - 1,
				8, ART_ALPHA_SEPARATE, NULL);
			art_render_svp (render, fill);
			gradient. a = 0.;
			gradient. b = 1. / (bbox.y1 - bbox.y0 + 1.);
			gradient. c = 0.;
			gradient.spread = ART_GRADIENT_REPEAT;
			gradient.n_stops = G_N_ELEMENTS (stops);
			gradient.stops = stops;

			go_color_to_artpix (stops[0].color,
				style->fill.u.gradient.start);
			go_color_to_artpix (stops[1].color,
				style->fill.u.gradient.end);
			art_render_gradient_linear (render,
				&gradient, ART_FILTER_NEAREST);
			art_render_invoke (render);
			break;

		case GOG_FILL_STYLE_IMAGE:
			g_warning ("unimplemented");

		case GOG_FILL_STYLE_NONE:
			break; /* impossible */
		}
		if (fill != NULL)
			art_svp_free (fill);
	}

	if (outline != NULL) {
		art_rgba_svp_alpha (outline,
			0, 0, prend->w, prend->h,
			style->outline.color,
			prend->pixels, prend->rowstride,
			NULL);
		art_svp_free (outline);
	}
}

static void
gog_renderer_pixbuf_class_init (GogRendererClass *rend_klass)
{
	GObjectClass *gobject_klass   = (GObjectClass *) rend_klass;

	parent_klass = g_type_class_peek_parent (rend_klass);
	gobject_klass->finalize	    = gog_renderer_pixbuf_finalize;
	rend_klass->begin_drawing	= gog_renderer_pixbuf_begin_drawing;
	rend_klass->draw_path	= gog_renderer_pixbuf_draw_path;
	rend_klass->draw_polygon	= gog_renderer_pixbuf_draw_polygon;
}

static void
gog_renderer_pixbuf_init (GogRendererPixbuf *prend)
{
	prend->buffer = NULL;
	prend->w = prend->h = 1; /* jsut in case */
}

GSF_CLASS (GogRendererPixbuf, gog_renderer_pixbuf,
	   gog_renderer_pixbuf_class_init, gog_renderer_pixbuf_init,
	   GOG_RENDERER_TYPE)

GdkPixbuf *
gog_renderer_pixbuf_get (GogRendererPixbuf *prend)
{
	g_return_val_if_fail (prend != NULL, NULL);

	return prend->buffer;
}

/**
 * gog_renderer_update :
 * @renderer :
 * @w :
 * @h :
 *
 * Returns TRUE if the size actually changed.
 **/
gboolean
gog_renderer_pixbuf_update (GogRendererPixbuf *prend, int w, int h)
{
	gboolean redraw = TRUE;
	GogView *view;
	GogViewAllocation allocation;

	g_return_val_if_fail (prend != NULL, FALSE);
	g_return_val_if_fail (prend->base.view != NULL, FALSE);

	view = prend->base.view;
	allocation.x = allocation.y = 0.;
	allocation.w = w;
	allocation.h = h;
	if (prend->w != w || prend->h != h) {
		prend->w = w;
		prend->h = h;
		prend->base.scale_x = w / prend->base.logical_width_pts;
		prend->base.scale_y = h / prend->base.logical_height_pts;
		prend->base.scale = MIN (prend->base.scale_x, prend->base.scale_y);

		/* make sure we dont try to queue an update while updating */
		prend->base.needs_update = TRUE;

		/* scale just changed need to recalculate sizes */
		gog_renderer_invalidate_size_requests (&prend->base);
		gog_view_size_allocate (view, &allocation);
		if (prend->buffer != NULL) {
			g_object_unref (prend->buffer);
			prend->buffer = NULL;
		}
	} else if (w != view->allocation.w || h != view->allocation.h)
		gog_view_size_allocate (view, &allocation);
	else
		redraw = gog_view_update_sizes (view);

	redraw |= prend->base.needs_update;
	prend->base.needs_update = FALSE;

	g_warning ("rend_pixbuf:update = %d", redraw);

	if (redraw) {
		gog_renderer_begin_drawing (&prend->base);
		gog_view_render	(view, NULL);
		gog_renderer_end_drawing  (&prend->base);
	}

	return redraw;
}
