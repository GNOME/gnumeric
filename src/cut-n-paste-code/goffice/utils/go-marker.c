/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-marker.c :
 *
 * Copyright (C) 2003 Emmanuel Pacaud (emmanuel.pacaud@univ-poitiers.fr)
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

#include "go-marker.h"

#include <libart_lgpl/art_render_gradient.h>
#include <libart_lgpl/art_render_svp.h>
#include <libart_lgpl/art_render_mask.h>
#include <libart_lgpl/art_svp_vpath_stroke.h>
#include <libart_lgpl/art_svp_vpath.h>
#include <libart_lgpl/art_affine.h>
#include <libart_lgpl/art_rgb_svp.h>

#include <goffice/utils/go-color.h>

#include <gnumeric-config.h>
#include <glade/glade-xml.h>
#include <widgets/widget-color-combo.h>
#include <widgets/widget-pixmap-combo.h>

#include <gdk-pixbuf/gdk-pixdata.h>

#include <src/mathfunc.h>
#include <src/gnumeric-i18n.h>

#include <gsf/gsf-impl-utils.h>

#define MARKER_DEFAULT_SIZE 5
#define MARKER_OUTLINE_WIDTH 0.1

typedef struct {
	GObjectClass	base;
} GOMarkerClass;

#define GO_MARKER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS((o),  GO_MARKER_TYPE, GOMarkerClass))

static ArtVpath const square_path[] = { 
	{ART_MOVETO, -1.0, -1.0},
	{ART_LINETO, -1.0,  1.0},
	{ART_LINETO,  1.0,  1.0},
	{ART_LINETO,  1.0, -1.0},
	{ART_LINETO, -1.0, -1.0},
	{ART_END   ,  0.0,  0.0}
};

static ArtVpath const diamond_path[] = { 
	{ART_MOVETO,  0.0, -1.0},
	{ART_LINETO,  1.0,  0.0},
	{ART_LINETO,  0.0,  1.0},
	{ART_LINETO, -1.0,  0.0},
	{ART_LINETO,  0.0, -1.0},
	{ART_END   ,  0.0,  0.0}
};

static ArtVpath const triangle_down_path[] = { 
	{ART_MOVETO, -1.0, -1.0},
	{ART_LINETO,  1.0, -1.0},
	{ART_LINETO,  0.0,  1.0},
	{ART_LINETO, -1.0, -1.0},
	{ART_END   ,  0.0,  0.0}
};

static ArtVpath const triangle_up_path[] = { 
	{ART_MOVETO,  0.0, -1.0},
	{ART_LINETO,  1.0,  1.0},
	{ART_LINETO, -1.0,  1.0},
	{ART_LINETO,  0.0, -1.0},
	{ART_END   ,  0.0,  0.0}
};

static ArtVpath const triangle_right_path[] = { 
	{ART_MOVETO, -1.0, -1.0},
	{ART_LINETO,  1.0,  0.0},
	{ART_LINETO, -1.0,  1.0},
	{ART_LINETO, -1.0, -1.0},
	{ART_END   ,  0.0,  0.0}
};

static ArtVpath const triangle_left_path[] = { 
	{ART_MOVETO,  1.0, -1.0},
	{ART_LINETO, -1.0,  0.0},
	{ART_LINETO,  1.0,  1.0},
	{ART_LINETO,  1.0, -1.0},
	{ART_END   ,  0.0,  0.0}
};

static ArtVpath const circle_path[] = {
	{ART_MOVETO,	1.000	,	0.000	},
	{ART_LINETO,	0.985	,	0.174	},
	{ART_LINETO,	0.940	,	0.342	},
	{ART_LINETO,	0.866	,	0.500	},
	{ART_LINETO,	0.766	,	0.643	},
	{ART_LINETO,	0.643	,	0.766	},
	{ART_LINETO,	0.500	,	0.866	},
	{ART_LINETO,	0.342	,	0.940	},
	{ART_LINETO,	0.174	,	0.985	},
	{ART_LINETO,	0.000	,	1.000	},
	{ART_LINETO,	-0.174	,	0.985	},
	{ART_LINETO,	-0.342	,	0.940	},
	{ART_LINETO,	-0.500	,	0.866	},
	{ART_LINETO,	-0.643	,	0.766	},
	{ART_LINETO,	-0.766	,	0.643	},
	{ART_LINETO,	-0.866	,	0.500	},
	{ART_LINETO,	-0.940	,	0.342	},
	{ART_LINETO,	-0.985	,	0.174	},
	{ART_LINETO,	-1.000	,	0.000	},
	{ART_LINETO,	-0.985	,	-0.174	},
	{ART_LINETO,	-0.940	,	-0.342	},
	{ART_LINETO,	-0.866	,	-0.500	},
	{ART_LINETO,	-0.766	,	-0.643	},
	{ART_LINETO,	-0.643	,	-0.766	},
	{ART_LINETO,	-0.500	,	-0.866	},
	{ART_LINETO,	-0.342	,	-0.940	},
	{ART_LINETO,	-0.174	,	-0.985	},
	{ART_LINETO,	-0.000	,	-1.000	},
	{ART_LINETO,	0.174	,	-0.985	},
	{ART_LINETO,	0.342	,	-0.940	},
	{ART_LINETO,	0.500	,	-0.866	},
	{ART_LINETO,	0.643	,	-0.766	},
	{ART_LINETO,	0.766	,	-0.643	},
	{ART_LINETO,	0.866	,	-0.500	},
	{ART_LINETO,	0.940	,	-0.342	},
	{ART_LINETO,	0.985	,	-0.174	},
	{ART_LINETO,	1.000	,	 0.000	},
	{ART_END,	0.000	,  	 0.000	}
};

static ArtVpath const x_path[] = { 
	{ART_MOVETO,  1.0,  1.0},
	{ART_LINETO, -1.0, -1.0},
	{ART_MOVETO,  1.0, -1.0},
	{ART_LINETO, -1.0,  1.0},
	{ART_END   ,  0.0,  0.0}
};

static ArtVpath const cross_path[] = { 
	{ART_MOVETO,  1.0,  0.0},
	{ART_LINETO, -1.0,  0.0},
	{ART_MOVETO,  0.0,  1.0},
	{ART_LINETO,  0.0, -1.0},
	{ART_END   ,  0.0,  0.0}
};

static ArtVpath const asterisk_path[] = { 
	{ART_MOVETO,  0.7,  0.7},
	{ART_LINETO, -0.7, -0.7},
	{ART_MOVETO,  0.7, -0.7},
	{ART_LINETO, -0.7,  0.7},
	{ART_MOVETO,  1.0,  0.0},
	{ART_LINETO, -1.0,  0.0},
	{ART_MOVETO,  0.0,  1.0},
	{ART_LINETO,  0.0, -1.0},
	{ART_END   ,  0.0,  0.0}
};

static ArtVpath const bar_path[] = {
	{ART_MOVETO, -1.0, -0.2},
	{ART_LINETO,  1.0, -0.2},
	{ART_LINETO,  1.0,  0.2},
	{ART_LINETO, -1.0,  0.2},
	{ART_LINETO, -1.0, -0.2},
	{ART_END   ,  0.0,  0.0}
};

static ArtVpath const half_bar_path[] = {
	{ART_MOVETO,  0.0, -0.2},
	{ART_LINETO,  1.0, -0.2},
	{ART_LINETO,  1.0,  0.2},
	{ART_LINETO,  0.0,  0.2},
	{ART_LINETO,  0.0, -0.2},
	{ART_END   ,  0.0,  0.0}
};

static ArtVpath const butterfly_path[] = {
	{ART_MOVETO, -1.0, -1.0},
	{ART_LINETO, -1.0,  1.0},
	{ART_LINETO,  0.0,  0.0},
	{ART_LINETO,  1.0,  1.0},
	{ART_LINETO,  1.0, -1.0},
	{ART_LINETO,  0.0,  0.0},
	{ART_LINETO, -1.0, -1.0},
	{ART_END   ,  0.0,  0.0}
};

static ArtVpath const hourglass_path[] = {
	{ART_MOVETO, -1.0, -1.0},
	{ART_LINETO,  1.0, -1.0},
	{ART_LINETO,  0.0,  0.0},
	{ART_LINETO,  1.0,  1.0},
	{ART_LINETO, -1.0,  1.0},
	{ART_LINETO,  0.0,  0.0},
	{ART_LINETO, -1.0, -1.0},
	{ART_END   ,  0.0,  0.0}
};

typedef struct
{
	char const *name;
	ArtVpath const *outline_path;
	ArtVpath const *fill_path;
} MarkerShape;
	
static MarkerShape const marker_shapes[GO_MARKER_LAST] = {
	{ N_("none"),		NULL, NULL},
	{ N_("square"),		square_path, square_path},
	{ N_("diamond"),	diamond_path, diamond_path},
	{ N_("triangle down"),	triangle_down_path, triangle_down_path},
	{ N_("triangle up"),	triangle_up_path, triangle_up_path},
	{ N_("triangle right"),	triangle_right_path, triangle_right_path},
	{ N_("triangle left"),	triangle_left_path, triangle_left_path},
	{ N_("circle"),		circle_path, circle_path},
	{ N_("x"),		x_path, square_path},
	{ N_("cross"),		cross_path, square_path},
	{ N_("asterisk"),	asterisk_path, square_path},
	{ N_("bar"), 		bar_path, bar_path},
	{ N_("half bar"),	half_bar_path, half_bar_path},
	{ N_("butterfly"),	butterfly_path, butterfly_path},
	{ N_("hourglass"),	hourglass_path, hourglass_path}
};

static struct {
	GOMarkerShape shape;
	char const *name;
} marker_shape_names[] = {
	{ GO_MARKER_NONE,           "none" },
	{ GO_MARKER_SQUARE,         "square" },
	{ GO_MARKER_DIAMOND,        "diamond" },
	{ GO_MARKER_TRIANGLE_DOWN,  "triangle-down" },
	{ GO_MARKER_TRIANGLE_UP,    "triangle-up" },
	{ GO_MARKER_TRIANGLE_RIGHT, "triangle-right" },
	{ GO_MARKER_TRIANGLE_LEFT,  "triangle-left" },
	{ GO_MARKER_CIRCLE,         "circle" },
	{ GO_MARKER_X,              "x" },
	{ GO_MARKER_CROSS,          "cross" },
	{ GO_MARKER_ASTERISK,       "asterisk" },
	{ GO_MARKER_BAR,            "bar" },
	{ GO_MARKER_HALF_BAR,       "half-bar" },
	{ GO_MARKER_BUTTERFLY,      "butterfly" },
	{ GO_MARKER_HOURGLASS,      "hourglass" }
};

static GObjectClass *marker_parent_klass;

static GdkPixbuf *
marker_create_pixbuf_with_size (GOMarker * marker, guint size)
{
	double scaling[6], translation[6], affine[6];
	guchar *pixels;
	int rowstride;
	ArtSVP *outline, *fill;
	double half_size;
	int pixbuf_size, offset;
	ArtVpath * outline_path;
	ArtVpath * fill_path;
	GdkPixbuf * pixbuf;

	if ((size < 1) ||
	    (marker->shape == GO_MARKER_NONE))
		return NULL;

	/* FIXME : markers look bad due to grey outline */

	offset = ceil ((double)size * MARKER_OUTLINE_WIDTH / 2.0);
	pixbuf_size = size + 1 + 2 * offset;
	half_size = (double)size / 2.0;
	
	art_affine_scale (scaling, half_size, half_size);
	art_affine_translate (translation, half_size + offset + .5, half_size + offset + .5);
	art_affine_multiply (affine, scaling, translation);
	
	outline_path = art_vpath_affine_transform (marker_shapes[marker->shape].outline_path, affine);
	fill_path = art_vpath_affine_transform (marker_shapes[marker->shape].fill_path, affine);

	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, pixbuf_size, pixbuf_size);
	pixels = gdk_pixbuf_get_pixels (pixbuf);
	rowstride = gdk_pixbuf_get_rowstride (pixbuf);

	gdk_pixbuf_fill (pixbuf, 0xffffff00);
	outline = art_svp_vpath_stroke (outline_path,
					ART_PATH_STROKE_JOIN_MITER,
					ART_PATH_STROKE_CAP_SQUARE,
				 	MARKER_OUTLINE_WIDTH * (double)size, 4, 0.5);
	fill = art_svp_from_vpath (fill_path);

	go_color_render_svp (marker->fill_color, fill, 0, 0, pixbuf_size, pixbuf_size, 
			     pixels, rowstride);
	go_color_render_svp (marker->outline_color, outline, 0, 0, pixbuf_size, pixbuf_size,
			     pixels, rowstride);

	art_svp_free (fill);
	art_svp_free (outline);

	g_free (outline_path);
	g_free (fill_path);

/*	{*/
/*		GError * error = NULL;*/
		
/*		if (!gdk_pixbuf_save (pixbuf, "test.png", "png", &error, NULL))*/
/*		{*/
/*			g_warning("%s", error->message);*/
/*			g_error_free (error);*/
/*		}*/
/*	}*/

	return pixbuf; 
}
	
static void
marker_update_pixbuf (GOMarker * marker)
{
	if (marker->pixbuf != NULL) {
		g_object_unref (G_OBJECT (marker->pixbuf));
		marker->pixbuf = NULL;
	}

	marker->pixbuf = marker_create_pixbuf_with_size (marker, marker->size);
}

static void
go_marker_finalize (GObject *obj)
{
	GOMarker * marker = GO_MARKER (obj);

	if (marker->pixbuf != NULL) {
		g_object_unref (G_OBJECT (marker->pixbuf));
		marker->pixbuf = NULL;
	}
	
	if (marker_parent_klass->finalize)
		marker_parent_klass->finalize (obj);
}

static void
go_marker_init (GOMarker * marker)
{
	marker->shape		= GO_MARKER_NONE;
	marker->outline_color	= RGBA_BLACK;
	marker->fill_color	= RGBA_WHITE;
	marker->size		= MARKER_DEFAULT_SIZE;
	marker->pixbuf = NULL;
}

static void
go_marker_class_init (GObjectClass *gobject_klass)
{
	marker_parent_klass = g_type_class_peek_parent (gobject_klass);
	gobject_klass->finalize	= go_marker_finalize;
}

GOMarkerShape
go_marker_shape_from_str (char const *name)
{
	unsigned i = G_N_ELEMENTS (marker_shape_names);
	while (i-- > 0)
		if (g_ascii_strcasecmp (marker_shape_names[i].name, name) == 0)
			return marker_shape_names[i].shape;
	return GO_MARKER_NONE;
}

char const *
go_marker_shape_as_str (GOMarkerShape shape)
{
	unsigned i = G_N_ELEMENTS (marker_shape_names);
	while (i-- > 0)
		if (marker_shape_names[i].shape == shape)
			return marker_shape_names[i].name;
	return "pattern";
}

void
go_marker_get_paths (GOMarker * marker,
		     ArtVpath const **outline_path,
		     ArtVpath const **fill_path)
{
	*outline_path = marker_shapes[marker->shape].outline_path;
	*fill_path = marker_shapes[marker->shape].fill_path;
}

GdkPixbuf const *
go_marker_get_pixbuf (GOMarker * marker)
{
	g_return_val_if_fail (IS_GO_MARKER (marker), NULL);

	if (marker->pixbuf == NULL)
		marker_update_pixbuf (marker);
	return marker->pixbuf;
}

GdkPixbuf const *
go_marker_get_pixbuf_with_size (GOMarker *marker, guint size)
{
	g_return_val_if_fail (IS_GO_MARKER (marker), NULL);

	return marker_create_pixbuf_with_size (marker, size);
}

GOMarkerShape 
go_marker_get_shape (GOMarker * marker)
{
	return marker->shape;
}

void
go_marker_set_shape (GOMarker *marker, GOMarkerShape shape)
{
	g_return_if_fail (IS_GO_MARKER (marker));
	if (marker->shape == shape)
		return;

	marker->shape = shape;
	if (marker->pixbuf != NULL) {
		g_object_unref (marker->pixbuf);
		marker->pixbuf = NULL;
	}
}
	
GOColor
go_marker_get_outline_color (GOMarker * marker)
{
	return marker->outline_color;
}

void
go_marker_set_outline_color (GOMarker *marker, GOColor color)
{
	g_return_if_fail (IS_GO_MARKER (marker));
	if (marker->outline_color == color)
		return;

	marker->outline_color = color;
	if (marker->pixbuf != NULL) {
		g_object_unref (marker->pixbuf);
		marker->pixbuf = NULL;
	}
}
	
GOColor
go_marker_get_fill_color (GOMarker * marker)
{
	return marker->fill_color;
}

void
go_marker_set_fill_color (GOMarker *marker, GOColor color)
{
	g_return_if_fail (IS_GO_MARKER (marker));
	
	if (marker->fill_color == color)
		return;
	marker->fill_color = color;
	if (marker->pixbuf != NULL) {
		g_object_unref (marker->pixbuf);
		marker->pixbuf = NULL;
	}
}
	
int
go_marker_get_size (GOMarker * marker)
{
	return marker->size;
}

double 
go_marker_get_outline_width (GOMarker * marker)
{
	return (double)marker->size * MARKER_OUTLINE_WIDTH;
}

void
go_marker_set_size (GOMarker *marker, int size)
{
	g_return_if_fail (IS_GO_MARKER (marker));
	g_return_if_fail (size >= 0);
	
	if (marker->size == size)
		return;
	marker->size = size;
	if (marker->pixbuf != NULL) {
		g_object_unref (marker->pixbuf);
		marker->pixbuf = NULL;
	}
}

void
go_marker_assign (GOMarker *dst, GOMarker const *src)
{
	if (src == dst)
		return;

	g_return_if_fail (GO_MARKER (src) != NULL);
	g_return_if_fail (GO_MARKER (dst) != NULL);

	dst->size		= src->size;
	dst->shape		= src->shape;
	dst->outline_color	= src->outline_color;
	dst->fill_color		= src->fill_color;

	if (dst->pixbuf != NULL)
		g_object_unref (G_OBJECT (src->pixbuf));
	dst->pixbuf = src->pixbuf;
	if (dst->pixbuf != NULL)
		g_object_ref (dst->pixbuf);
}	
	
GOMarker *
go_marker_dup (GOMarker *src)
{
	GOMarker *dst = go_marker_new ();
	go_marker_assign (dst, src);
	return dst;
}

GOMarker *
go_marker_new (void)
{
	return g_object_new (GO_MARKER_TYPE, NULL);
}

GSF_CLASS (GOMarker, go_marker,
	   go_marker_class_init, go_marker_init,
	   G_TYPE_OBJECT)

/*---------------------------------------------------------------------------*/

#define SELECTOR_PIXBUF_SIZE 20
#define SELECTOR_MARKER_SIZE 15

gpointer
go_marker_selector (GOColor outline_color, GOColor fill_color,
		    GOMarkerShape default_shape)
{
	static PixmapComboElement elements[] = {
		{ NULL, NULL, GO_MARKER_NONE},
		{ NULL, NULL, GO_MARKER_SQUARE },
		{ NULL, NULL, GO_MARKER_DIAMOND},
		{ NULL, NULL, GO_MARKER_TRIANGLE_DOWN},
		{ NULL, NULL, GO_MARKER_TRIANGLE_UP},
		{ NULL, NULL, GO_MARKER_TRIANGLE_RIGHT},
		{ NULL, NULL, GO_MARKER_TRIANGLE_LEFT},
		{ NULL, NULL, GO_MARKER_CIRCLE},
		{ NULL, NULL, GO_MARKER_X},
		{ NULL, NULL, GO_MARKER_CROSS},
		{ NULL, NULL, GO_MARKER_ASTERISK},
		{ NULL, NULL, GO_MARKER_BAR},
		{ NULL, NULL, GO_MARKER_HALF_BAR},
		{ NULL, NULL, GO_MARKER_BUTTERFLY},
		{ NULL, NULL, GO_MARKER_HOURGLASS},
		{ NULL, NULL, -1 } /* fill in with Auto */
	};

	guint i, w, h,shape, length;
	char const *shape_name;
	gpointer data;
	GdkPixdata pixdata;
	GtkWidget *widget;
	GdkPixbuf const *mbuf = NULL;
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, 
					    TRUE, 8, 
					    SELECTOR_PIXBUF_SIZE, 
					    SELECTOR_PIXBUF_SIZE);
	GOMarker *marker = go_marker_new ();

	go_marker_set_fill_color (marker, fill_color);
	go_marker_set_outline_color (marker, outline_color);
	go_marker_set_size (marker, 15);

	for (i = 0; i < G_N_ELEMENTS (elements); i++) {
		if (i == G_N_ELEMENTS (elements) -1) {
			elements[i].id = -default_shape;
			shape = default_shape;
			shape_name = g_strdup_printf ("%s (%s)",
				_("Automatic"), /* avoid violating string freeze */
				marker_shapes [shape].name);
		} else {
			shape = elements[i].id;
			shape_name = marker_shapes [shape].name;
		}

		go_marker_set_shape (marker, shape);
		mbuf = go_marker_get_pixbuf (marker);
		gdk_pixbuf_fill (pixbuf, 0); /* in case the fill colours have alpha = 0 */
		if (mbuf != NULL)  {
			w = gdk_pixbuf_get_width (mbuf);
			h = gdk_pixbuf_get_height (mbuf);
			gdk_pixbuf_copy_area (mbuf, 0, 0, w, h, pixbuf, 
				(SELECTOR_PIXBUF_SIZE - w) / 2,
				(SELECTOR_PIXBUF_SIZE - h) / 2);
		}

		data = gdk_pixdata_from_pixbuf (&pixdata, pixbuf, FALSE);
		elements[i].inline_gdkpixbuf = gdk_pixdata_serialize (&pixdata, &length);
		elements[i].untranslated_tooltip = shape_name;
		g_free (data);
	}
	g_object_unref (marker);
	g_object_unref (pixbuf);

	widget = pixmap_combo_new (elements, 4, 4, TRUE);
	for (i = 0; i < G_N_ELEMENTS (elements); i++)
		g_free ((char *)elements[i].inline_gdkpixbuf);
	gnm_combo_box_set_tearable (GNM_COMBO_BOX (widget), FALSE);

	g_free ((char *)elements [i-1].untranslated_tooltip);
	return widget;
}
