/*
 * gnm-so-polygon.c: Polygons
 *
 * Copyright (C) 2005 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
#include <gnumeric.h>
#include <gnm-so-polygon.h>
#include <sheet-object-impl.h>
#include <sheet.h>
#include <parse-util.h>

#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>
#include <math.h>

#define GNM_SO_POLYGON(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SO_POLYGON_TYPE, GnmSOPolygon))
#define GNM_SO_POLYGON_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k),	GNM_SO_POLYGON_TYPE, GnmSOPolygonClass))

typedef struct {
	SheetObject base;
	GOStyle *style;
	GArray	 *points;
} GnmSOPolygon;
typedef SheetObjectClass GnmSOPolygonClass;

#ifdef GNM_WITH_GTK
#include <gnm-pane.h>
static void
so_polygon_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	GocItem *item = sheet_object_view_get_item (sov);

	if (visible) {
		SheetObject		*so   = sheet_object_view_get_so (sov);
		GnmSOPolygon const	*sop  = GNM_SO_POLYGON (so);
		unsigned		 i, n;
		GocPoints		*pts;
		double x_scale, y_scale, x_translate, y_translate;
		double const *src;

		if (sop->points == NULL)
			return;

		n = sop->points->len / 2;
		if (n == 0)
			return;

		pts = goc_points_new (n);
		x_scale = fabs (coords[2] - coords[0]);
		y_scale = fabs (coords[3] - coords[1]);
		x_translate = MIN (coords[0], coords[2]),
		y_translate = MIN (coords[1], coords[3]);

		src = &g_array_index (sop->points, double, 0);
		for (i = 0 ; i < n; src += 2, i++) {
			pts->points[i].x = x_translate + x_scale * src[0];
			pts->points[i].y = y_translate + y_scale * src[1];
		}

		goc_item_set (item, "points", pts, NULL);
		goc_points_unref (pts);
		goc_item_show (item);
	} else
		goc_item_hide (item);
}

static void
so_polygon_goc_view_class_init (SheetObjectViewClass *sov_klass)
{
	sov_klass->set_bounds	= so_polygon_view_set_bounds;
}

typedef SheetObjectView	PolygonGocView;
typedef SheetObjectViewClass	PolygonGocViewClass;
static GSF_CLASS (PolygonGocView, so_polygon_goc_view,
	so_polygon_goc_view_class_init, NULL,
	GNM_SO_VIEW_TYPE)

#endif /* GNM_WITH_GTK */

/*****************************************************************************/

static SheetObjectClass *gnm_so_polygon_parent_class;
enum {
	SOP_PROP_0,
	SOP_PROP_STYLE,
	SOP_PROP_POINTS,
	SOP_PROP_DOCUMENT
};

static GOStyle *
sop_default_style (void)
{
	GOStyle *res = go_style_new ();
	res->interesting_fields = GO_STYLE_OUTLINE | GO_STYLE_FILL;
	res->line.width = 0; /* hairline */
	res->line.color = GO_COLOR_BLACK;
	res->line.dash_type = GO_LINE_SOLID; /* anything but 0 */
	res->line.join = CAIRO_LINE_JOIN_ROUND;
	res->fill.type = GO_STYLE_FILL_PATTERN;
	go_pattern_set_solid (&res->fill.pattern, GO_COLOR_WHITE);
	return res;
}

#ifdef GNM_WITH_GTK
#include <sheet-control-gui.h>
#include <dialogs/dialogs.h>

static void
cb_gnm_so_polygon_style_changed (GocItem *view, GnmSOPolygon const *sop)
{
	GocItem *item = sheet_object_view_get_item (GNM_SO_VIEW (view));
	GOStyle const *style = sop->style;
	goc_item_set (item, "style", style, NULL);
}

static SheetObjectView *
gnm_so_polygon_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmSOPolygon *sop = GNM_SO_POLYGON (so);
	GocItem *item = goc_item_new (
		gnm_pane_object_group (GNM_PANE (container)),
		so_polygon_goc_view_get_type (),
		NULL);
	goc_item_new (GOC_GROUP (item),
		GOC_TYPE_POLYGON,
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
	dialog_so_styled (scg_wbcg (GNM_SCG (sc)), G_OBJECT (so),
			  sop_default_style (),
			  _("Polygon Properties"), SO_STYLED_STYLE_ONLY);
}

#endif /* GNM_WITH_GTK */

static void
gnm_so_polygon_draw_cairo (SheetObject const *so, cairo_t *cr,
	double width, double height)
{
}

static void
gnm_so_polygon_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
			      GnmConventions const *convs)
{
	GnmSOPolygon const *sop = GNM_SO_POLYGON (so);
	unsigned int ui;

	for (ui = 0; ui + 1 < (sop->points ? sop->points->len : 0); ui += 2) {
		double x = g_array_index (sop->points, double, ui);
		double y = g_array_index (sop->points, double, ui + 1);
		gsf_xml_out_start_element (output, "Point");
		go_xml_out_add_double (output, "x", x);
		go_xml_out_add_double (output, "y", y);
		gsf_xml_out_end_element (output); /* </Point> */
	}

	gsf_xml_out_start_element (output, "Style");
	go_persist_sax_save (GO_PERSIST (sop->style), output);
	gsf_xml_out_end_element (output); /* </Style> */
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

	switch (param_id) {
	case SOP_PROP_STYLE: {
		GOStyle *style = go_style_dup (g_value_get_object (value));
		style->interesting_fields = GO_STYLE_OUTLINE | GO_STYLE_FILL;
		g_object_unref (sop->style);
		sop->style = style;
		break;
	}
	case SOP_PROP_POINTS:
		points = g_value_get_pointer (value);
		if (!points)
			points = g_array_new (FALSE, TRUE, sizeof (double));

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
	case SOP_PROP_DOCUMENT:
		g_value_set_object (value, sheet_object_get_sheet (GNM_SO (obj))->workbook);
		break;
	default:
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
	SheetObjectClass *so_class = GNM_SO_CLASS (gobject_class);

	gnm_so_polygon_parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize		= gnm_so_polygon_finalize;
	gobject_class->set_property	= gnm_so_polygon_set_property;
	gobject_class->get_property	= gnm_so_polygon_get_property;
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
                 g_param_spec_object ("style", NULL, NULL, GO_TYPE_STYLE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, SOP_PROP_POINTS,
                 g_param_spec_pointer ("points", NULL, NULL,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, SOP_PROP_DOCUMENT,
		g_param_spec_object ("document", NULL, NULL, GO_TYPE_DOC,
			GSF_PARAM_STATIC | G_PARAM_READABLE));
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
	   GNM_SO_TYPE)
