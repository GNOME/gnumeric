/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnm-so-line.c: Lines, arrows, arcs
 *
 * Copyright (C) 2004 Jody Goldberg (jody@gnome.org)
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
#include "gnumeric.h"
#include "gnm-so-line.h"
#include "sheet-object-impl.h"

#include <src/xml-io.h>
#include <goffice/graph/gog-style.h>
#include <goffice/utils/go-color.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n.h>
#include <string.h>

#define GNM_SO_LINE(o)		(G_TYPE_CHECK_INSTANCE_CAST((o), GNM_SO_LINE_TYPE, GnmSOLine))
#define GNM_SO_LINE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST ((k),   GNM_SO_LINE_TYPE, GnmSOLineClass))

/*****************************************************************************/
typedef struct {
	GOColor	color;
	double  a, b, c;
} GOArrow;

static void
go_arrow_init (GOArrow *res, double a, double b, double c)
{
	res->color = RGBA_BLACK;
	res->a = a;
	res->b = b;
	res->c = c;
}

static void
go_arrow_copy (GOArrow *dst, GOArrow const *src)
{
	dst->color = src->color;
	dst->a = src->a;
	dst->b = src->b;
	dst->c = src->c;
}

/*****************************************************************************/
typedef struct {
	SheetObject base;

	GogStyle *style;
	GOArrow	  start_arrow, end_arrow;
} GnmSOLine;
typedef SheetObjectClass GnmSOLineClass;

static SheetObjectClass *gnm_so_line_parent_class;

#ifdef WITH_GTK
#include <libfoocanvas/foo-canvas.h>
#include <libfoocanvas/foo-canvas-line.h>
#include <libfoocanvas/foo-canvas-util.h>
static void
so_line_view_destroy (SheetObjectView *sov)
{
	gtk_object_destroy (GTK_OBJECT (sov));
}
static void
so_line_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem	*view = FOO_CANVAS_ITEM (sov);
	SheetObject	*so = sheet_object_view_get_so (sov);
	GogStyleLine const *style = &GNM_SO_LINE (so)->style->line;

	if (visible &&
	    style->color != 0 && style->width >= 0 && style->pattern != 0) {
		FooCanvasPoints *points = foo_canvas_points_new (2);
		points->coords[0] = coords[0];
		points->coords[1] = coords[1];
		points->coords[2] = coords[2];
		points->coords[3] = coords[3];
		foo_canvas_item_set (view, "points", points, NULL);
		foo_canvas_points_free (points);
		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}

static void
so_line_foo_view_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= so_line_view_destroy;
	sov_iface->set_bounds	= so_line_view_set_bounds;
}
typedef FooCanvasLine		LineFooView;
typedef FooCanvasLineClass	LineFooViewClass;
static GSF_CLASS_FULL (LineFooView, so_line_foo_view,
	NULL, NULL,
	FOO_TYPE_CANVAS_LINE, 0,
	GSF_INTERFACE (so_line_foo_view_init, SHEET_OBJECT_VIEW_TYPE))
#endif /* WITH_GTK */
enum {
	SOL_PROP_0,
	SOL_PROP_STYLE,
	SOL_PROP_START_ARROW,
	SOL_PROP_END_ARROW,
        SOL_PROP_IS_ARROW
};

static GogStyle *
sol_default_style ()
{
	GogStyle *res = gog_style_new ();
	res->interesting_fields = GOG_STYLE_LINE;
	res->line.width   = 0; /* hairline */
	res->line.color   = RGBA_BLACK;
	res->line.pattern = 1; /* anything but 0 */
	return res;
}

#ifdef WITH_GTK
#include <src/sheet-control-gui.h>
#include <src/dialogs/dialogs.h>
#include <src/gnumeric-pane.h>
#include <src/gnumeric-simple-canvas.h>
#include <src/gnumeric-canvas.h>
#include <goffice/utils/go-color.h>

static void
gnm_so_line_user_config (SheetObject *so, SheetControl *sc)
{
	dialog_so_styled (scg_get_wbcg (SHEET_CONTROL_GUI (sc)), G_OBJECT (so),
		GNM_SO_LINE (so)->style, sol_default_style (),
		_("Line/Arrow Properties"));
}

static void
cb_gnm_so_line_changed (GnmSOLine const *sol,
			G_GNUC_UNUSED GParamSpec *pspec,
			FooCanvasItem *item)
{
	GogStyleLine const *style = &sol->style->line;
	GdkColor buf, *gdk = NULL;
	
	if (style->color != 0 && style->width >= 0 && style->pattern != 0)
		gdk = go_color_to_gdk (style->color, &buf);

	if (style->width > 0.)	/* in pts */
		foo_canvas_item_set (item,
			"width_units",		style->width,
			"fill_color_gdk",	gdk,
			NULL);
	else /* hairline 1 pixel that ignores zoom */
		foo_canvas_item_set (item,
			"width_pixels",		1,
			"fill_color_gdk",	gdk,
			NULL);
	foo_canvas_item_set (item,
		"arrow_shape_a",	sol->end_arrow.a,
		"arrow_shape_b",	sol->end_arrow.b,
		"arrow_shape_c",	sol->end_arrow.c,
		NULL);
}

static SheetObjectView *
gnm_so_line_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmSOLine const *sol = GNM_SO_LINE (so);
	GnmCanvas *gcanvas = ((GnmPane *)container)->gcanvas;
	FooCanvasItem *item = foo_canvas_item_new (gcanvas->object_views,
		so_line_foo_view_get_type (),
		"last_arrowhead",	(sol->end_arrow.a != 0.),
		NULL);
	cb_gnm_so_line_changed (sol, NULL, item);
	g_signal_connect_object (G_OBJECT (sol),
		"notify", G_CALLBACK (cb_gnm_so_line_changed),
		item, 0);
	return gnm_pane_object_register (so, item, TRUE);
}

static void
gnm_so_line_print (SheetObject const *so, GnomePrintContext *ctx,
		   double width, double height)
{
	GnmSOLine *sol = GNM_SO_LINE (so);
	GogStyleLine const *style = &sol->style->line;
	double x1, y1, x2, y2;

	if (style->color == 0 || style->width < 0 || style->pattern == 0)
		return;

	switch (so->anchor.direction) {
	case SO_DIR_UP_RIGHT:
	case SO_DIR_DOWN_RIGHT:
		x1 = 0.;
		x2 = width;
		break;
	case SO_DIR_UP_LEFT:
	case SO_DIR_DOWN_LEFT:
		x1 = width;
		x2 = 0.;
		break;
	default:
		g_warning ("Cannot guess direction!");
		return;
	}

	switch (so->anchor.direction) {
	case SO_DIR_UP_LEFT:
	case SO_DIR_UP_RIGHT:
		y1 = -height;
		y2 = 0.;
		break;
	case SO_DIR_DOWN_LEFT:
	case SO_DIR_DOWN_RIGHT:
		y1 = 0.;
		y2 = -height;
		break;
	default:
		g_warning ("Cannot guess direction!");
		return;
	}

	gnome_print_setrgbcolor (ctx,
		style->color / (double) 0xffff,
		style->color / (double) 0xffff,
		style->color / (double) 0xffff);

	if (sol->end_arrow.c > 0.) {
		double phi;

		phi = atan2 (y2 - y1, x2 - x1) - M_PI_2;

		gnome_print_gsave (ctx);
		gnome_print_translate (ctx, x2, y2);
		gnome_print_rotate (ctx, phi / (2 * M_PI) * 360);
		gnome_print_setlinewidth (ctx, 1.0);
		gnome_print_newpath (ctx);
		gnome_print_moveto (ctx, 0.0, 0.0);
		gnome_print_lineto (ctx, -sol->end_arrow.c, -sol->end_arrow.b);
		gnome_print_lineto (ctx, 0.0, -sol->end_arrow.a);
		gnome_print_lineto (ctx, sol->end_arrow.c, -sol->end_arrow.b);
		gnome_print_closepath (ctx);
		gnome_print_fill (ctx);
		gnome_print_grestore (ctx);

		/* Make the line shorter so that the arrow won't be
		 * on top of a (perhaps quite fat) line.  */
		x2 += sol->end_arrow.a * sin (phi);
		y2 -= sol->end_arrow.a * cos (phi);
	}

	gnome_print_setlinewidth (ctx, style->width);
	gnome_print_newpath (ctx);
	gnome_print_moveto (ctx, x1, y1);
	gnome_print_lineto (ctx, x2, y2);
	gnome_print_stroke (ctx);
}

#endif /* WITH_GTK */

static gboolean
gnm_so_line_read_xml_dom (SheetObject *so, char const *typename,
			  XmlParseContext const *ctxt,
			  xmlNodePtr node)
{
	GnmSOLine *sol = GNM_SO_LINE (so);
	double width, a, b, c;
	xmlNode *child;

	if (xml_node_get_double (node, "ArrowShapeA", &a) &&
	    xml_node_get_double (node, "ArrowShapeB", &b) &&
	    xml_node_get_double (node, "ArrowShapeC", &c))
		go_arrow_init (&sol->end_arrow, a, b, c);

	if (NULL != (child = e_xml_get_child_by_name (node, "Style"))) /* new version */
		return !gog_persist_dom_load (GOG_PERSIST (sol->style), child);
	/* Old 1.0 and 1.2 */
	xml_node_get_gocolor (node, "FillColor", &sol->style->line.color);
	if (xml_node_get_double  (node, "Width", &width))
		sol->style->line.width = width;

	return FALSE;
}

static gboolean
gnm_so_line_write_xml_dom (SheetObject const *so,
			   XmlParseContext const *ctxt,
			   xmlNodePtr node)
{
	GnmSOLine const *sol = GNM_SO_LINE (so);
	xmlNode *child;

	/* YES FillColor, this is for backwards compat */
	xml_node_set_gocolor (node, "FillColor", sol->style->line.color);
	xml_node_set_double (node,  "Width", sol->style->line.width, -1);

	if (sol->end_arrow.c > 0.) {
		xml_node_set_int (node, "Type", 2);
		xml_node_set_double (node, "ArrowShapeA", sol->end_arrow.a, -1);
		xml_node_set_double (node, "ArrowShapeB", sol->end_arrow.b, -1);
		xml_node_set_double (node, "ArrowShapeC", sol->end_arrow.c, -1);
	} else
		xml_node_set_int (node, "Type", 1);

	child = xmlNewDocNode (node->doc, NULL, "Style", NULL);
	gog_persist_dom_save (GOG_PERSIST (sol->style), child);
	xmlAddChild (node, child);
	return FALSE;
}
static void
gnm_so_line_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	GnmSOLine const *sol = GNM_SO_LINE (so);

	gnm_xml_out_add_gocolor (output, "FillColor", sol->style->line.color);
	gsf_xml_out_add_float (output, "Width", sol->style->line.width, -1);
	if (sol->end_arrow.c > 0.) {
		gsf_xml_out_add_int (output, "Type", 2);
		gsf_xml_out_add_float (output, "ArrowShapeA", sol->end_arrow.a, -1);
		gsf_xml_out_add_float (output, "ArrowShapeB", sol->end_arrow.b, -1);
		gsf_xml_out_add_float (output, "ArrowShapeC", sol->end_arrow.c, -1);
	} else
		gsf_xml_out_add_int (output, "Type", 1);

	gsf_xml_out_start_element (output, "Style");
	gog_persist_sax_save (GOG_PERSIST (sol->style), output);
	gsf_xml_out_end_element (output); /* </Style> */
}

static void
gnm_so_line_copy (SheetObject *dst, SheetObject const *src)
{
	GnmSOLine const *sol = GNM_SO_LINE (src);
	GnmSOLine   *new_sol = GNM_SO_LINE (dst);

	g_object_unref (new_sol->style);
	new_sol->style = gog_style_dup (sol->style);
	go_arrow_copy (&new_sol->start_arrow, &sol->start_arrow);
	go_arrow_copy (&new_sol->end_arrow, &sol->end_arrow);
}

static void
gnm_so_line_set_property (GObject *obj, guint param_id,
			  GValue const *value, GParamSpec *pspec)
{
	GnmSOLine *sol = GNM_SO_LINE (obj);
	switch (param_id) {
	case SOL_PROP_STYLE:
		g_object_unref (sol->style);
		sol->style = g_object_ref (g_value_get_object (value));
		sol->style->interesting_fields = GOG_STYLE_LINE;
		break;
	case SOL_PROP_START_ARROW:
		go_arrow_copy (&sol->start_arrow, g_value_get_pointer (value));
		break;
	case SOL_PROP_END_ARROW:
		go_arrow_copy (&sol->end_arrow, g_value_get_pointer (value));
		break;
	case SOL_PROP_IS_ARROW:
		if (g_value_get_boolean (value))
			go_arrow_init (&sol->end_arrow, 8., 10., 3.);
		else
			go_arrow_init (&sol->end_arrow, 0., 0., 0.);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}

static void
gnm_so_line_get_property (GObject *obj, guint param_id,
			  GValue  *value,  GParamSpec *pspec)
{
	GnmSOLine *sol = GNM_SO_LINE (obj);
	switch (param_id) {
	case SOL_PROP_STYLE:
		g_value_set_object (value, sol->style);
		break;
	case SOL_PROP_START_ARROW:
		g_value_set_pointer (value, &sol->start_arrow);
		break;
	case SOL_PROP_END_ARROW:
		g_value_set_pointer (value, &sol->end_arrow);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
gnm_so_line_finalize (GObject *object)
{
	GnmSOLine *sol = GNM_SO_LINE (object);
	g_object_unref (sol->style);
	sol->style = NULL;
	G_OBJECT_CLASS (gnm_so_line_parent_class)->finalize (object);
}

static void
gnm_so_line_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *so_class  = SHEET_OBJECT_CLASS (gobject_class);

	gnm_so_line_parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize		= gnm_so_line_finalize;
	gobject_class->set_property	= gnm_so_line_set_property;
	gobject_class->get_property	= gnm_so_line_get_property;
	so_class->read_xml_dom		= gnm_so_line_read_xml_dom;
	so_class->write_xml_dom		= gnm_so_line_write_xml_dom;
	so_class->write_xml_sax		= gnm_so_line_write_xml_sax;
	so_class->copy			= gnm_so_line_copy;
	so_class->rubber_band_directly  = TRUE;
	so_class->xml_export_name	= "SheetObjectGraphic";

#ifdef WITH_GTK
	so_class->user_config		= gnm_so_line_user_config;
	so_class->new_view		= gnm_so_line_new_view;
	so_class->print			= gnm_so_line_print;
#endif /* WITH_GTK */

        g_object_class_install_property (gobject_class, SOL_PROP_STYLE,
                 g_param_spec_object ("style", NULL, NULL, GOG_STYLE_TYPE,
			(G_PARAM_READABLE | G_PARAM_WRITABLE)));
        g_object_class_install_property (gobject_class, SOL_PROP_START_ARROW,
                 g_param_spec_pointer ("start-arrow", NULL, NULL,
			(G_PARAM_READABLE | G_PARAM_WRITABLE)));
        g_object_class_install_property (gobject_class, SOL_PROP_END_ARROW,
                 g_param_spec_pointer ("end-arrow", NULL, NULL,
			(G_PARAM_READABLE | G_PARAM_WRITABLE)));
        g_object_class_install_property (gobject_class, SOL_PROP_IS_ARROW,
                 g_param_spec_boolean ("is-arrow", NULL, NULL, FALSE,
			(G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY)));
}

static void
gnm_so_line_init (GObject *obj)
{
	GnmSOLine *sol  = GNM_SO_LINE (obj);
	sol->style = sol_default_style ();
	go_arrow_init (&sol->start_arrow, 0., 0., 0.);
	go_arrow_init (&sol->end_arrow,   0., 0., 0.);

	SHEET_OBJECT (obj)->anchor.direction = SO_DIR_NONE_MASK;
}

GSF_CLASS (GnmSOLine, gnm_so_line,
	   gnm_so_line_class_init, gnm_so_line_init,
	   SHEET_OBJECT_TYPE);
