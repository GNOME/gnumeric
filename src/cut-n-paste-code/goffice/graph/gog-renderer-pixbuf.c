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
#include <goffice/utils/go-units.h>

#include <libart_lgpl/art_render_gradient.h>
#include <libart_lgpl/art_render_svp.h>
#include <libart_lgpl/art_render_mask.h>
#include <pango/pangoft2.h>
#include <gsf/gsf-impl-utils.h>

#include <math.h>

struct _GogRendererPixbuf {
	GogRenderer base;

	int w, h;
	GdkPixbuf *buffer;
	guchar    *pixels; /* from pixbuf */
	int	   rowstride;

	PangoContext *pango_context;
};

typedef GogRendererClass GogRendererPixbufClass;

static GObjectClass *parent_klass;

static void
gog_renderer_pixbuf_finalize (GObject *obj)
{
	GogRendererPixbuf *prend = GOG_RENDERER_PIXBUF (obj);

	if (prend->buffer != NULL) {
		g_object_unref (prend->buffer);
		prend->buffer = NULL;
	}

	if (prend->pango_context != NULL) {
		g_object_unref (prend->pango_context);
		prend->pango_context = NULL;
	}

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static void
gog_renderer_pixbuf_draw_path (GogRenderer *renderer, ArtVpath *path)
{
	GogRendererPixbuf *prend = GOG_RENDERER_PIXBUF (renderer);
	GogStyle *style = renderer->cur_style;
	double width = gog_renderer_line_size (renderer, style->line.width);
	ArtSVP *svp = art_svp_vpath_stroke (path,
		ART_PATH_STROKE_JOIN_MITER, ART_PATH_STROKE_CAP_SQUARE,
		width, 4, 0.5);
	go_color_render_svp (style->line.color, svp,
		0, 0, prend->w, prend->h,
		prend->pixels, prend->rowstride);
	art_svp_free (svp);
}

static ArtRender *
gog_art_renderer_new (GogRendererPixbuf *prend)
{
	return art_render_new (0, 0, prend->w, prend->h,
		prend->pixels, prend->rowstride,
		gdk_pixbuf_get_n_channels (prend->buffer) - 1,
		8, ART_ALPHA_SEPARATE, NULL);
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
	GdkPixbuf *image;
	GError *err = NULL;
	gint i, j, imax, jmax, w, h, x, y;

	if (!narrow && style->outline.width >= 0.)
		outline = art_svp_vpath_stroke (path,
			ART_PATH_STROKE_JOIN_MITER, ART_PATH_STROKE_CAP_SQUARE,
			gog_renderer_line_size (renderer, style->outline.width), 4, 0.5);

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
		case GOG_FILL_STYLE_PATTERN:
			go_pattern_render_svp (&style->fill.u.pattern.pat,
				fill, 0, 0, prend->w, prend->h,
				prend->pixels, prend->rowstride);
			break;

		case GOG_FILL_STYLE_GRADIENT: {
			double dx, dy;

			art_vpath_bbox_drect (path, &bbox);
			dx = bbox.x1 - bbox.x0;
			dy = bbox.y1 - bbox.y0;

			render = gog_art_renderer_new (prend);
			art_render_svp (render, fill);
			if (style->fill.u.gradient.dir < 4) {
				gradient.a = 0.;
				gradient.b = 1. / (dy ? dy : 1);
				gradient.c = -(gradient.a * bbox.x0 + gradient.b * bbox.y0);
			} else if (style->fill.u.gradient.dir < 8) {
				gradient.a = 1. / (dx ? dx : 1);
				gradient.b = 0.;
				gradient.c = -(gradient.a * bbox.x0 + gradient.b * bbox.y0);
			} else if (style->fill.u.gradient.dir < 12) {
				double d = dx * dx + dy * dy;
				if (!d) d = 1;
				gradient.a = dx / d;
				gradient.b = dy / d;
				gradient.c = -(gradient.a * bbox.x0 + gradient.b * bbox.y0);
			} else {
				double d = dx * dx + dy * dy;
				if (!d) d = 1;
				gradient.a = -dx / d;
				gradient.b = dy / d;
				/* Note: this gradient is anchored at (x1,y0).  */
				gradient.c = -(gradient.a * bbox.x1 + gradient.b * bbox.y0);
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
			break;
		}

		case GOG_FILL_STYLE_IMAGE: {
			if (!style->fill.u.image.image_file)
				break;
			
			double dx, dy;
			art_vpath_bbox_drect (path, &bbox);
			dx = bbox.x1 - bbox.x0;
			dy = bbox.y1 - bbox.y0;
			image = gdk_pixbuf_new_from_file (style->fill.u.image.image_file, &err);
			if (err != NULL)
				break;
			switch (style->fill.u.image.type) {
			case GOG_IMAGE_STRETCHED:
				gdk_pixbuf_composite (image, prend->buffer,
					bbox.x0, bbox.y0, dx, dy, bbox.x0, bbox.y0,
					dx / gdk_pixbuf_get_width (image),
					dy / gdk_pixbuf_get_height (image),
					GDK_INTERP_HYPER, 255);
				break;

			case GOG_IMAGE_WALLPAPER:
				imax = dx / (w = gdk_pixbuf_get_width (image));
				jmax = dy / (h = gdk_pixbuf_get_height (image));
				x = bbox.x0;
				for (i = 0; i < imax; i++) {
					y = bbox.y0;
					for (j = 0; j < jmax; j++) {
						gdk_pixbuf_copy_area (image, 0, 0, w, h,
								      prend->buffer, x, y);
						y += h;
					}
					gdk_pixbuf_copy_area (image, 0, 0, w,
							      prend->h % h, prend->buffer, x, y);
					x += w;
				}
				y = bbox.y0;
				for (j = 0; j < jmax; j++) {
					gdk_pixbuf_copy_area (image, 0, 0, (int)dx % w, h,
							      prend->buffer, x, y);
					y += h;
				}
				gdk_pixbuf_copy_area (image, 0, 0, (int)dx % w, (int)dy % h,
						      prend->buffer, x, y);
				break;
			}

			g_object_unref (image);

			break;
		}

		case GOG_FILL_STYLE_NONE:
			break; /* impossible */
		}
		if (fill != NULL)
			art_svp_free (fill);
	}

	if (outline != NULL) {
		go_color_render_svp (style->outline.color, outline,
			0, 0, prend->w, prend->h,
			prend->pixels, prend->rowstride);
		art_svp_free (outline);
	}
}

static PangoLayout *
make_layout (GogRendererPixbuf *prend, char const *text)
{
	PangoLayout *layout = pango_layout_new (prend->pango_context);
	PangoAttribute *attr_zoom;
	PangoAttrList  *attrs = NULL;

	/* Assemble our layout. */
	pango_layout_set_font_description (layout,
		pango_context_get_font_description (prend->pango_context));
	pango_layout_set_text (layout, text, -1);
	attr_zoom = pango_attr_scale_new (prend->base.zoom);
	attr_zoom->start_index = 0;
	attr_zoom->end_index = -1;
	attrs = pango_attr_list_new ();
	pango_attr_list_insert (attrs, attr_zoom);
	pango_layout_set_attributes (layout, attrs);
	pango_attr_list_unref (attrs);

	return layout;
}

static void
gog_renderer_pixbuf_draw_text (GogRenderer *rend, ArtPoint *pos,
			       char const *text, GogViewRequisition *size)
{
	FT_Bitmap ft_bitmap;
	ArtRender *render;
	ArtPixMaxDepth color[4];
	GogRendererPixbuf *prend = GOG_RENDERER_PIXBUF (rend);
	PangoRectangle rect;
	PangoLayout   *layout = make_layout (prend, text);

	pango_layout_get_pixel_extents (layout, &rect, NULL);
	if (rect.width == 0 || rect.height == 0)
		return;
	ft_bitmap.rows         = rect.height;
	ft_bitmap.width        = rect.width;
	ft_bitmap.pitch        = (rect.width+3) & ~3;
	ft_bitmap.buffer       = g_malloc0 (ft_bitmap.rows * ft_bitmap.pitch);
	ft_bitmap.num_grays    = 256;
	ft_bitmap.pixel_mode   = ft_pixel_mode_grays;
	ft_bitmap.palette_mode = 0;
	ft_bitmap.palette      = NULL;
	pango_ft2_render_layout (&ft_bitmap, layout, -rect.x, -rect.y);
	g_object_unref (layout);

	render = gog_art_renderer_new (prend);
	go_color_to_artpix (color, RGBA_BLACK);
	art_render_image_solid (render, color);
	art_render_mask (render,
		pos->x + rect.x, pos->y,
		pos->x + rect.x + rect.width,
		pos->y + rect.height,
		ft_bitmap.buffer, ft_bitmap.pitch);
	art_render_invoke (render);
}

static void
gog_renderer_pixbuf_measure_text (GogRenderer *rend,
				  char const *text, GogViewRequisition *size)
{
	PangoRectangle  rect;
	PangoLayout    *layout = make_layout ((GogRendererPixbuf *)rend, text);
	pango_layout_get_pixel_extents (layout, &rect, NULL);
	g_object_unref (layout);

	size->w = rect.width;
	size->h = rect.height;
}

static void
gog_renderer_pixbuf_class_init (GogRendererClass *rend_klass)
{
	GObjectClass *gobject_klass   = (GObjectClass *) rend_klass;

	parent_klass = g_type_class_peek_parent (rend_klass);
	gobject_klass->finalize	  = gog_renderer_pixbuf_finalize;
	rend_klass->draw_path	  = gog_renderer_pixbuf_draw_path;
	rend_klass->draw_polygon  = gog_renderer_pixbuf_draw_polygon;
	rend_klass->draw_text	  = gog_renderer_pixbuf_draw_text;
	rend_klass->measure_text  = gog_renderer_pixbuf_measure_text;
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

#if 0 /* An initial non-working attempt to use different dpi to render
	 different zooms */

/* fontmaps are reasonably expensive use a cache to share them */
static GHashTable *fontmap_cache = NULL; /* PangoFT2FontMap hashed by y_dpi */
static gboolean
cb_remove_entry (gpointer key, PangoFT2FontMap *value, PangoFT2FontMap *target)
{
	return value == target;
}
static void
cb_map_is_gone (gpointer data, GObject *where_the_object_was)
{
	g_warning ("fontmap %p is gone",where_the_object_was);
	g_hash_table_foreach_steal (fontmap_cache,
		(GHRFunc) cb_remove_entry, where_the_object_was);
}
static void
cb_weak_unref (GObject *fontmap)
{
	g_object_weak_unref (fontmap, cb_map_is_gone, NULL);
}
static PangoFT2FontMap *
fontmap_from_cache (double x_dpi, double y_dpi)
{
	PangoFT2FontMap *fontmap = NULL;
	int key_dpi = floor (y_dpi + .5);
	gpointer key = GUINT_TO_POINTER (key_dpi);

	if (fontmap_cache != NULL)
		fontmap = g_hash_table_lookup (fontmap_cache, key);
	else
		fontmap_cache = g_hash_table_new_full (g_direct_hash, g_direct_equal,
			NULL, (GDestroyNotify) cb_weak_unref);

	if (fontmap == NULL) {
		fontmap = PANGO_FT2_FONT_MAP (pango_ft2_font_map_new ());
		pango_ft2_font_map_set_resolution (fontmap, x_dpi, y_dpi);
		g_object_weak_ref (G_OBJECT (fontmap), cb_map_is_gone, NULL);
		g_hash_table_insert (fontmap_cache, key, fontmap);
	} else
		g_object_ref (fontmap);

	g_warning ("fontmap %d = %p", key_dpi, fontmap);
	return fontmap;
}
#endif

/**
 * gog_renderer_update :
 * @renderer :
 * @w :
 * @h :
 *
 * Returns TRUE if the size actually changed.
 **/
gboolean
gog_renderer_pixbuf_update (GogRendererPixbuf *prend, int w, int h, double zoom)
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
		double dpi_x, dpi_y;

		prend->w = w;
		prend->h = h;
		prend->base.scale_x = w / prend->base.logical_width_pts;
		prend->base.scale_y = h / prend->base.logical_height_pts;
		prend->base.scale = MIN (prend->base.scale_x, prend->base.scale_y);
		prend->base.zoom  = zoom;
		dpi_x = gog_renderer_pt2r_x (&prend->base, GO_IN_TO_PT (1.))
			/ zoom;
		dpi_y = gog_renderer_pt2r_y (&prend->base, GO_IN_TO_PT (1.))
			/ zoom;

		if (prend->buffer != NULL) {
			g_object_unref (prend->buffer);
			prend->buffer = NULL;
		}
		if (prend->pango_context != NULL) {
			g_object_unref (prend->pango_context);
			prend->pango_context = NULL;
		}

		prend->pango_context = pango_ft2_font_map_create_context (
			PANGO_FT2_FONT_MAP (pango_ft2_font_map_for_display ()));

		/* make sure we dont try to queue an update while updating */
		prend->base.needs_update = TRUE;

		/* scale just changed need to recalculate sizes */
		gog_renderer_invalidate_size_requests (&prend->base);
		gog_view_size_allocate (view, &allocation);
	} else if (w != view->allocation.w || h != view->allocation.h)
		gog_view_size_allocate (view, &allocation);
	else
		redraw = gog_view_update_sizes (view);

	redraw |= prend->base.needs_update;
	prend->base.needs_update = FALSE;

	gog_debug (0, g_warning ("rend_pixbuf:update = %d", redraw););

	if (redraw) {
		if (prend->buffer == NULL) {
			prend->buffer = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
							prend->w, prend->h);
			prend->pixels    = gdk_pixbuf_get_pixels (prend->buffer);
			prend->rowstride = gdk_pixbuf_get_rowstride (prend->buffer);
		}
		gdk_pixbuf_fill (prend->buffer, 0);

		gog_view_render	(view, NULL);
	}

	return redraw;
}
