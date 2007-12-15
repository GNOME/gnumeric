/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnm-so-polygon.c: Polygons
 *
 * Copyright (C) 2005 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gnm-so-polygon.h"
#include "sheet-object-impl.h"
#include "xml-io.h"

#include <goffice/utils/go-libxml-extras.h>
#include <goffice/graph/gog-style.h>
#include <goffice/utils/go-color.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>
#include <math.h>

#define CXML2C(s) ((char const *)(s))

static inline gboolean
attr_eq (const xmlChar *a, const char *s)
{
	return !strcmp (CXML2C (a), s);
}

#define GNM_SO_POLYGON(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SO_POLYGON_TYPE, GnmSOPolygon))
#define GNM_SO_POLYGON_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k),	GNM_SO_POLYGON_TYPE, GnmSOPolygonClass))

typedef struct {
	SheetObject base;
	GogStyle *style;
	GArray	 *points;
} GnmSOPolygon;
typedef SheetObjectClass GnmSOPolygonClass;

#ifdef GNM_WITH_GTK
#include "gnm-pane.h"
#include <goffice/cut-n-paste/foocanvas/foo-canvas.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-polygon.h>
static void
so_polygon_view_destroy (SheetObjectView *sov)
{
	gtk_object_destroy (GTK_OBJECT (sov));
}
static void
so_polygon_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem *view = FOO_CANVAS_ITEM (sov);

	if (visible) {
		SheetObject		*so   = sheet_object_view_get_so (sov);
		GnmSOPolygon const	*sop  = GNM_SO_POLYGON (so);
		unsigned		 i;
		FooCanvasPoints		*pts;
		double *dst, x_scale, y_scale, x_translate, y_translate;
		double const *src;

		if (sop->points == NULL)
			return;

		i = sop->points->len / 2;
		pts = foo_canvas_points_new (i);
		x_scale = fabs (coords[2] - coords[0]);
		y_scale = fabs (coords[3] - coords[1]);
		x_translate = MIN (coords[0], coords[2]),
		y_translate = MIN (coords[1], coords[3]);

		src = &g_array_index (sop->points, double, 0);
		dst = pts->coords;
		for ( ; i-- > 0; dst += 2, src += 2) {
			dst[0] = x_translate + x_scale * src[0];
			dst[1] = y_translate + y_scale * src[1];
		}

		foo_canvas_item_set (view, "points", pts, NULL);
		foo_canvas_points_free (pts);
		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}

static void
so_polygon_foo_view_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= so_polygon_view_destroy;
	sov_iface->set_bounds	= so_polygon_view_set_bounds;
}
typedef FooCanvasPolygon	PolygonFooView;
typedef FooCanvasPolygonClass	PolygonFooViewClass;
static GSF_CLASS_FULL (PolygonFooView, so_polygon_foo_view,
	NULL, NULL, NULL, NULL, NULL,
	FOO_TYPE_CANVAS_POLYGON, 0,
	GSF_INTERFACE (so_polygon_foo_view_init, SHEET_OBJECT_VIEW_TYPE))
#endif /* GNM_WITH_GTK */

/*****************************************************************************/

static SheetObjectClass *gnm_so_polygon_parent_class;
enum {
	SOP_PROP_0,
	SOP_PROP_STYLE,
	SOP_PROP_POINTS
};

static GogStyle *
sop_default_style (void)
{
	GogStyle *res = gog_style_new ();
	res->interesting_fields = GOG_STYLE_OUTLINE | GOG_STYLE_FILL;
	res->outline.width = 0; /* hairline */
	res->outline.color = RGBA_BLACK;
	res->outline.dash_type = GO_LINE_SOLID; /* anything but 0 */
	res->fill.type = GOG_FILL_STYLE_PATTERN;
	go_pattern_set_solid (&res->fill.pattern, RGBA_WHITE);
	return res;
}

#ifdef GNM_WITH_GTK
#include <sheet-control-gui.h>
#include <dialogs/dialogs.h>

static void
cb_gnm_so_polygon_style_changed (FooCanvasItem *view, GnmSOPolygon const *sop)
{
	GogStyle const *style = sop->style;
	GdkColor outline_buf, *outline_gdk = NULL;
	GdkColor fill_buf, *fill_gdk = NULL;

	if (style->outline.color != 0 &&
	    style->outline.width >= 0 &&
	    style->outline.dash_type != GO_LINE_NONE)
		outline_gdk = go_color_to_gdk (style->outline.color, &outline_buf);

	if (style->fill.type != GOG_FILL_STYLE_NONE)
		fill_gdk = go_color_to_gdk (style->fill.pattern.back, &fill_buf);

	if (style->outline.width > 0.)	/* in pts */
		foo_canvas_item_set (view,
			"width-units",		style->outline.width,
			"outline-color-gdk",	outline_gdk,
			"fill-color-gdk",	fill_gdk,
			NULL);
	else /* hairline 1 pixel that ignores zoom */
		foo_canvas_item_set (view,
			"width-pixels",		1,
			"outline-color-gdk",	outline_gdk,
			"fill-color-gdk",	fill_gdk,
			NULL);

}
static SheetObjectView *
gnm_so_polygon_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmSOPolygon *sop = GNM_SO_POLYGON (so);
	FooCanvasItem *item = foo_canvas_item_new (
		gnm_pane_object_group (GNM_PANE (container)),
		so_polygon_foo_view_get_type (),
		/* "join_style",	GDK_JOIN_ROUND, */
		NULL);
	cb_gnm_so_polygon_style_changed (item, sop);
	g_signal_connect_object (sop,
		"notify::style", G_CALLBACK (cb_gnm_so_polygon_style_changed),
		item, 0);
	return gnm_pane_object_register (so, item, TRUE);
}

static void
gnm_so_polygon_user_config (SheetObject *so, SheetControl *sc)
{
	dialog_so_styled (scg_wbcg (SHEET_CONTROL_GUI (sc)), G_OBJECT (so),
		GNM_SO_POLYGON (so)->style, sop_default_style (),
		_("Polygon Properties"));
}

#endif /* GNM_WITH_GTK */

static void
gnm_so_polygon_draw_cairo (SheetObject const *so, cairo_t *cr,
	double width, double height)
{
}

static gboolean
gnm_so_polygon_read_xml_dom (SheetObject *so, char const *typename,
			     XmlParseContext const *ctxt, xmlNodePtr node)
{
	GnmSOPolygon *sop = GNM_SO_POLYGON (so);
	xmlNodePtr ptr;
	double vals[2];

	g_array_set_size (sop->points, 0);
	for (ptr = node->xmlChildrenNode; ptr != NULL ; ptr = ptr->next)
		if (!xmlIsBlankNode (ptr) && ptr->name &&
		    attr_eq (ptr->name, "Point") &&
		    xml_node_get_double	(ptr, "x", vals + 0) &&
		    xml_node_get_double	(ptr, "y", vals + 1))
			g_array_append_vals (sop->points, vals, 2);

	return gnm_so_polygon_parent_class->
		read_xml_dom (so, typename, ctxt, node);
}

static void
gnm_so_polygon_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	/* TODO */
	return gnm_so_polygon_parent_class->write_xml_sax (so, output);
}

static void
gnm_so_polygon_copy (SheetObject *dst, SheetObject const *src)
{
	GnmSOPolygon const *sop = GNM_SO_POLYGON (src);
	GnmSOPolygon   *new_sop = GNM_SO_POLYGON (dst);
	unsigned i = sop->points->len;

	g_array_set_size (new_sop->points, i);
	while (i-- > 0)
		g_array_index (new_sop->points, double, i) =
			g_array_index (sop->points, double, i);
	gnm_so_polygon_parent_class->copy (dst, src);
}

static void
gnm_so_polygon_set_property (GObject *obj, guint param_id,
			    GValue const *value, GParamSpec *pspec)
{
	GnmSOPolygon *sop = GNM_SO_POLYGON (obj);
	GArray *points;
	GogStyle *style;

	switch (param_id) {
	case SOP_PROP_STYLE:
		style = sop->style;
		sop->style = g_object_ref (g_value_get_object (value));
		sop->style->interesting_fields = GOG_STYLE_OUTLINE | GOG_STYLE_FILL;
		g_object_unref (style);
		break;
	case SOP_PROP_POINTS:
		points = g_value_get_pointer (value);

		g_return_if_fail (points != NULL);

		if (sop->points != points) {
			g_array_free (sop->points, TRUE);
			sop->points = points;
		}
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}

static void
gnm_so_polygon_get_property (GObject *obj, guint param_id,
			    GValue *value, GParamSpec *pspec)
{
	GnmSOPolygon *sop = GNM_SO_POLYGON (obj);
	switch (param_id) {
	case SOP_PROP_STYLE:
		g_value_set_object (value, sop->style);
		break;
	case SOP_PROP_POINTS:
		g_value_set_pointer (value, sop->points);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
gnm_so_polygon_finalize (GObject *object)
{
	GnmSOPolygon *sop = GNM_SO_POLYGON (object);

	g_object_unref (sop->style);
	sop->style = NULL;
	if (sop->points != NULL) {
		g_array_free (sop->points, TRUE);
		sop->points = NULL;
	}
	G_OBJECT_CLASS (gnm_so_polygon_parent_class)->finalize (object);
}

static void
gnm_so_polygon_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (gobject_class);

	gnm_so_polygon_parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize		= gnm_so_polygon_finalize;
	gobject_class->set_property	= gnm_so_polygon_set_property;
	gobject_class->get_property	= gnm_so_polygon_get_property;
	so_class->read_xml_dom		= gnm_so_polygon_read_xml_dom;
	so_class->write_xml_sax		= gnm_so_polygon_write_xml_sax;
	so_class->copy			= gnm_so_polygon_copy;
	so_class->rubber_band_directly	= FALSE;
	so_class->xml_export_name	= "SheetObjectPolygon";

#ifdef GNM_WITH_GTK
	so_class->new_view		= gnm_so_polygon_new_view;
	so_class->user_config		= gnm_so_polygon_user_config;
#endif /* GNM_WITH_GTK */
	so_class->draw_cairo	= gnm_so_polygon_draw_cairo;

        g_object_class_install_property (gobject_class, SOP_PROP_STYLE,
                 g_param_spec_object ("style", NULL, NULL, GOG_STYLE_TYPE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, SOP_PROP_POINTS,
                 g_param_spec_pointer ("points", NULL, NULL,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
}

static void
gnm_so_polygon_init (GObject *obj)
{
	static double const initial_coords [] = {
		0., 0.,		1., 0.,
		1., 1.,		0., 1.
	};
	GnmSOPolygon *sop = GNM_SO_POLYGON (obj);
	sop->points = g_array_sized_new (FALSE, TRUE, sizeof (double),
		G_N_ELEMENTS (initial_coords));
	sop->style = sop_default_style ();
	g_array_append_vals (sop->points,
		initial_coords, G_N_ELEMENTS (initial_coords));
}

GSF_CLASS (GnmSOPolygon, gnm_so_polygon,
	   gnm_so_polygon_class_init, gnm_so_polygon_init,
	   SHEET_OBJECT_TYPE);
