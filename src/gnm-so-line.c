/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnm-so-line.c: Lines, arrows, arcs
 *
 * Copyright (C) 2004-2007 Jody Goldberg (jody@gnome.org)
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
#include "gnm-so-line.h"
#include "sheet-object-impl.h"
#include "xml-io.h"
#include "xml-sax.h"

#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define CXML2C(s) ((char const *)(s))

static inline gboolean
attr_eq (const xmlChar *a, const char *s)
{
	return !strcmp (CXML2C (a), s);
}

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
	res->color = GO_COLOR_BLACK;
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

	GOStyle *style;
	GOArrow	  start_arrow, end_arrow;
} GnmSOLine;
typedef SheetObjectClass GnmSOLineClass;

static SheetObjectClass *gnm_so_line_parent_class;

#ifdef GNM_WITH_GTK
static void
so_line_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	GocItem	*view = GOC_ITEM (sov), *item = GOC_ITEM (GOC_GROUP (view)->children->data);
	SheetObject	*so = sheet_object_view_get_so (sov);
	GOStyleLine const *style = &GNM_SO_LINE (so)->style->line;

	sheet_object_direction_set (so, coords);

	if (visible &&
	    style->color != 0 && style->width >= 0 && style->dash_type != GO_LINE_NONE) {
		goc_item_set (item,
		              "x0", coords[0],
		              "y0", coords[1],
		              "x1", coords[2],
		              "y1", coords[3],
		              NULL);
		goc_item_show (view);
	} else
		goc_item_hide (view);
}

static void
so_line_goc_view_class_init (SheetObjectViewClass *sov_klass)
{
	sov_klass->set_bounds	= so_line_view_set_bounds;
}
typedef SheetObjectView		LineGocView;
typedef SheetObjectViewClass	LineGocViewClass;
static GSF_CLASS (LineGocView, so_line_goc_view,
	so_line_goc_view_class_init, NULL,
	SHEET_OBJECT_VIEW_TYPE)

#endif /* GNM_WITH_GTK */
enum {
	SOL_PROP_0,
	SOL_PROP_STYLE,
	SOL_PROP_START_ARROW,
	SOL_PROP_END_ARROW,
        SOL_PROP_IS_ARROW
};

static GOStyle *
sol_default_style (void)
{
	GOStyle *res = go_style_new ();
	res->interesting_fields = GO_STYLE_LINE;
	res->line.width   = 0; /* hairline */
	res->line.color   = GO_COLOR_BLACK;
	res->line.dash_type = GO_LINE_SOLID; /* anything but 0 */
	return res;
}

#ifdef GNM_WITH_GTK
#include <sheet-control-gui.h>
#include <dialogs/dialogs.h>
#include <gnumeric-simple-canvas.h>
#include <gnm-pane.h>

static void
gnm_so_line_user_config (SheetObject *so, SheetControl *sc)
{
	dialog_so_styled (scg_wbcg (SHEET_CONTROL_GUI (sc)), G_OBJECT (so),
			  GNM_SO_LINE (so)->style, sol_default_style (),
			  _("Line/Arrow Properties"), SO_STYLED_STYLE_ONLY);
}

static void
cb_gnm_so_line_changed (GnmSOLine const *sol,
			G_GNUC_UNUSED GParamSpec *pspec,
			GocItem *item)
{
	item = GOC_ITEM (GOC_GROUP (item)->children->data);
	goc_item_set (item,
	        "style",		sol->style,
		"arrow-shape-a",	sol->end_arrow.a,
		"arrow-shape-b",	sol->end_arrow.b,
		"arrow-shape-c",	sol->end_arrow.c,
		NULL);
}

static SheetObjectView *
gnm_so_line_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmSOLine const *sol = GNM_SO_LINE (so);
	GocItem *item = goc_item_new (
		gnm_pane_object_group (GNM_PANE (container)),
		so_line_goc_view_get_type (),
		NULL);
	goc_item_new (GOC_GROUP (item),
		GOC_TYPE_LINE,
		"arrowhead",	(sol->end_arrow.a != 0.),
		NULL);
	cb_gnm_so_line_changed (sol, NULL, item);
	g_signal_connect_object (G_OBJECT (sol),
		"notify", G_CALLBACK (cb_gnm_so_line_changed),
		item, 0);
	return gnm_pane_object_register (so, item, TRUE);
}

#endif /* GNM_WITH_GTK */

static void
gnm_so_line_draw_cairo (SheetObject const *so, cairo_t *cr,
			double width, double height)
{
	GnmSOLine *sol = GNM_SO_LINE (so);
	GOStyleLine const *style = &sol->style->line;
	double x1, y1, x2, y2;

	if (style->color == 0 || style->width < 0 || style->dash_type == GO_LINE_NONE)
		return;

	switch (so->anchor.base.direction) {
	case GOD_ANCHOR_DIR_UP_RIGHT:
	case GOD_ANCHOR_DIR_DOWN_RIGHT:
		x1 = 0.;
		x2 = width;
		break;
	case GOD_ANCHOR_DIR_UP_LEFT:
	case GOD_ANCHOR_DIR_DOWN_LEFT:
		x1 = width;
		x2 = 0.;
		break;
	default:
		g_warning ("Cannot guess direction!");
		return;
	}

	switch (so->anchor.base.direction) {
	case GOD_ANCHOR_DIR_UP_LEFT:
	case GOD_ANCHOR_DIR_UP_RIGHT:
		y1 = height;
		y2 = 0.;
		break;
	case GOD_ANCHOR_DIR_DOWN_LEFT:
	case GOD_ANCHOR_DIR_DOWN_RIGHT:
		y1 = 0.;
		y2 = height;
		break;
	default:
		g_warning ("Cannot guess direction!");
		return;
	}

	cairo_set_line_width (cr, (style->width)? style->width: 1.);
	cairo_set_source_rgba (cr,
		GO_COLOR_UINT_R(style->color),
		GO_COLOR_UINT_B(style->color),
		GO_COLOR_UINT_G(style->color),
		GO_COLOR_UINT_A(style->color));

	if (sol->end_arrow.c > 0.) {
		double phi;

		phi = atan2 (y2 - y1, x2 - x1) - M_PI_2;

		cairo_save (cr);
		cairo_translate (cr, x2, y2);
		cairo_rotate (cr, phi);
		cairo_set_line_width (cr, 1.0);
		cairo_new_path (cr);
		cairo_move_to (cr, 0.0, 0.0);
		cairo_line_to (cr, -sol->end_arrow.c, -sol->end_arrow.b);
		cairo_line_to (cr, 0.0, -sol->end_arrow.a);
		cairo_line_to (cr, sol->end_arrow.c, -sol->end_arrow.b);
		cairo_close_path (cr);
		cairo_fill (cr);
		cairo_restore (cr);

		/* Make the line shorter so that the arrow won't be
		 * on top of a (perhaps quite fat) line.  */
		x2 += sol->end_arrow.a * sin (phi);
		y2 -= sol->end_arrow.a * cos (phi);
	}

	cairo_set_line_width (cr, style->width);
	cairo_new_path (cr);
	cairo_move_to (cr, x1, y1);
	cairo_line_to (cr, x2, y2);
	cairo_stroke (cr);
}

static gboolean
gnm_so_line_read_xml_dom (SheetObject *so, char const *typename,
			  XmlParseContext const *ctxt,
			  xmlNodePtr node)
{
	GnmSOLine *sol = GNM_SO_LINE (so);
	double width, a, b, c;
	xmlNode *child;

	if (go_xml_node_get_double (node, "ArrowShapeA", &a) &&
	    go_xml_node_get_double (node, "ArrowShapeB", &b) &&
	    go_xml_node_get_double (node, "ArrowShapeC", &c))
		go_arrow_init (&sol->end_arrow, a, b, c);

	if (NULL != (child = go_xml_get_child_by_name (node, "Style"))) /* new version */
		return !go_persist_dom_load (GO_PERSIST (sol->style), child);
	/* Old 1.0 and 1.2 */
	go_xml_node_get_gocolor (node, "FillColor", &sol->style->line.color);
	if (go_xml_node_get_double  (node, "Width", &width))
		sol->style->line.width = width;

	return FALSE;
}

static void
gnm_so_line_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
			   GnmConventions const *convs)
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
	go_persist_sax_save (GO_PERSIST (sol->style), output);
	gsf_xml_out_end_element (output); /* </Style> */
}

static void
sol_sax_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	SheetObject *so = gnm_xml_in_cur_obj (xin);
	GnmSOLine *sol = GNM_SO_LINE (so);
	go_persist_prep_sax (GO_PERSIST (sol->style), xin, attrs);
}

static void
gnm_so_line_prep_sax_parser (SheetObject *so, GsfXMLIn *xin,
			     xmlChar const **attrs,
			     GnmConventions const *convs)
{
	static GsfXMLInNode const dtd[] = {
	  GSF_XML_IN_NODE (STYLE, STYLE, -1, "Style",	GSF_XML_NO_CONTENT, &sol_sax_style, NULL),
	  GSF_XML_IN_NODE_END
	};
	static GsfXMLInDoc *doc = NULL;
	GnmSOLine *sol = GNM_SO_LINE (so);
	double tmp, arrow_a = -1., arrow_b = -1., arrow_c = -1.;
	int type = 0;

	if (NULL == doc)
		doc = gsf_xml_in_doc_new (dtd, NULL);
	gsf_xml_in_push_state (xin, doc, NULL, NULL, attrs);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		/* Old 1.0 and 1.2 */
		if (gnm_xml_attr_double (attrs, "Width", &tmp))
			sol->style->line.width = tmp;
		else if (attr_eq (attrs[0], "FillColor"))
			go_color_from_str (CXML2C (attrs[1]), &sol->style->line.color);
		else if (gnm_xml_attr_int (attrs, "Type", &type)) ;
		else if (gnm_xml_attr_double (attrs, "ArrowShapeA", &arrow_a)) ;
		else if (gnm_xml_attr_double (attrs, "ArrowShapeB", &arrow_b)) ;
		else if (gnm_xml_attr_double (attrs, "ArrowShapeC", &arrow_c)) ;

	/* 2 == arrow */
	if (type == 2 && arrow_a >= 0. && arrow_b >= 0. && arrow_c >= 0.)
		go_arrow_init (&sol->end_arrow, arrow_a, arrow_b, arrow_c);
}

static void
gnm_so_line_copy (SheetObject *dst, SheetObject const *src)
{
	GnmSOLine const *sol = GNM_SO_LINE (src);
	GnmSOLine   *new_sol = GNM_SO_LINE (dst);

	g_object_unref (new_sol->style);
	new_sol->style = go_style_dup (sol->style);
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
		sol->style->interesting_fields = GO_STYLE_LINE;
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
	case SOL_PROP_IS_ARROW:
		g_value_set_boolean (value, sol->end_arrow.c > 0);
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
	so_class->write_xml_sax		= gnm_so_line_write_xml_sax;
	so_class->prep_sax_parser	= gnm_so_line_prep_sax_parser;
	so_class->copy			= gnm_so_line_copy;
	so_class->rubber_band_directly  = TRUE;
	so_class->xml_export_name	= "SheetObjectGraphic";

#ifdef GNM_WITH_GTK
	so_class->draw_cairo	= gnm_so_line_draw_cairo;
	so_class->user_config		= gnm_so_line_user_config;
	so_class->new_view		= gnm_so_line_new_view;
#endif /* GNM_WITH_GTK */

        g_object_class_install_property (gobject_class, SOL_PROP_STYLE,
                 g_param_spec_object ("style", NULL, NULL, GO_TYPE_STYLE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, SOL_PROP_START_ARROW,
                 g_param_spec_pointer ("start-arrow", NULL, NULL,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, SOL_PROP_END_ARROW,
                 g_param_spec_pointer ("end-arrow", NULL, NULL,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, SOL_PROP_IS_ARROW,
                 g_param_spec_boolean ("is-arrow", NULL, NULL, FALSE,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gnm_so_line_init (GObject *obj)
{
	GnmSOLine *sol  = GNM_SO_LINE (obj);
	sol->style = sol_default_style ();
	go_arrow_init (&sol->start_arrow, 0., 0., 0.);
	go_arrow_init (&sol->end_arrow,   0., 0., 0.);

	SHEET_OBJECT (obj)->anchor.base.direction = GOD_ANCHOR_DIR_NONE_MASK;
}

GSF_CLASS (GnmSOLine, gnm_so_line,
	   gnm_so_line_class_init, gnm_so_line_init,
	   SHEET_OBJECT_TYPE)
