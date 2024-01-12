/*
 * gnm-so-line.c: Lines, arrows, arcs
 *
 * Copyright (C) 2004-2007 Jody Goldberg (jody@gnome.org)
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
#include <gnm-so-line.h>
#include <sheet-object-impl.h>
#include <gutils.h>
#include <xml-sax.h>

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
	GocItem	*view = GOC_ITEM (sov);
	GocItem *item = sheet_object_view_get_item (sov);
	SheetObject	*so = sheet_object_view_get_so (sov);
	GOStyleLine const *style = &GNM_SO_LINE (so)->style->line;
	double scale = goc_canvas_get_pixels_per_unit (view->canvas);

	sheet_object_direction_set (so, coords);

	if (visible &&
	    style->color != 0 && style->width >= 0 && style->dash_type != GO_LINE_NONE) {
		goc_item_set (item,
		              "x0", coords[0] / scale,
		              "y0", coords[1] / scale,
		              "x1", coords[2] / scale,
		              "y1", coords[3] / scale,
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
	GNM_SO_VIEW_TYPE)

#endif /* GNM_WITH_GTK */
enum {
	SOL_PROP_0,
	SOL_PROP_STYLE,
	SOL_PROP_START_ARROW,
	SOL_PROP_END_ARROW
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
	dialog_so_styled (scg_wbcg (GNM_SCG (sc)), G_OBJECT (so),
			  sol_default_style (),
			  _("Line/Arrow Properties"), SO_STYLED_LINE);
}

static void
cb_gnm_so_line_changed (GnmSOLine const *sol,
			G_GNUC_UNUSED GParamSpec *pspec,
			GocItem *item)
{
	item = sheet_object_view_get_item (GNM_SO_VIEW (item));
	goc_item_set (item,
		      "start-arrow", &sol->start_arrow,
		      "end-arrow", &sol->end_arrow,
		      "style", sol->style,
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
	goc_item_new (GOC_GROUP (item), GOC_TYPE_LINE, NULL);
	cb_gnm_so_line_changed (sol, NULL, item);
	g_signal_connect_object (G_OBJECT (sol),
		"notify", G_CALLBACK (cb_gnm_so_line_changed),
		item, 0);
	return gnm_pane_object_register (so, item, TRUE);
}

#endif /* GNM_WITH_GTK */

static void
draw_arrow (const GOArrow *arrow, cairo_t *cr,
	    double *x, double *y, double phi)
{
	double dx, dy;

	cairo_save (cr);
	cairo_translate (cr, *x, *y);
	go_arrow_draw (arrow, cr, &dx, &dy, phi);
	*x += dx;
	*y += dy;
	cairo_restore (cr);
}

static void
gnm_so_line_draw_cairo (SheetObject const *so, cairo_t *cr,
			double width, double height)
{
	GnmSOLine *sol = GNM_SO_LINE (so);
	GOStyle const *style = sol->style;
	double x1, y1, x2, y2;
	double phi;

	if (style->line.color == 0 ||
	    style->line.width < 0 ||
	    style->line.dash_type == GO_LINE_NONE)
		return;

	if ((so->anchor.base.direction & GOD_ANCHOR_DIR_H_MASK) == GOD_ANCHOR_DIR_RIGHT) {
		x1 = 0.;
		x2 = width;
	} else {
		x1 = width;
		x2 = 0.;
	}

	if ((so->anchor.base.direction & GOD_ANCHOR_DIR_V_MASK) == GOD_ANCHOR_DIR_DOWN) {
		y1 = 0.;
		y2 = height;
	} else {
		y1 = height;
		y2 = 0.;
	}

	cairo_set_source_rgba (cr, GO_COLOR_TO_CAIRO (style->line.color));

	phi = atan2 (y2 - y1, x2 - x1) - M_PI_2;
	draw_arrow (&sol->start_arrow, cr, &x1, &y1, phi + M_PI);
	draw_arrow (&sol->end_arrow, cr, &x2, &y2, phi);

	cairo_move_to (cr, x1, y1);
	cairo_line_to (cr, x2, y2);
	if (go_style_set_cairo_line (style, cr))
		cairo_stroke (cr);
	else
		cairo_new_path (cr);
}

static void
write_xml_sax_arrow (GOArrow const *arrow, const char *prefix,
		     GsfXMLOut *output)
{
	const char *typename = go_arrow_type_as_str (arrow->typ);
	char *attr;

	if (!typename || arrow->typ == GO_ARROW_NONE)
		return;

	attr = g_strconcat (prefix, "ArrowType", NULL);
	gsf_xml_out_add_cstr (output, attr, typename);
	g_free (attr);

	attr = g_strconcat (prefix, "ArrowShapeA", NULL);
	go_xml_out_add_double (output, attr, arrow->a);
	g_free (attr);

	attr = g_strconcat (prefix, "ArrowShapeB", NULL);
	go_xml_out_add_double (output, attr, arrow->b);
	g_free (attr);

	attr = g_strconcat (prefix, "ArrowShapeC", NULL);
	go_xml_out_add_double (output, attr, arrow->c);
	g_free (attr);
}

static void
gnm_so_line_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
			   GnmConventions const *convs)
{
	GnmSOLine const *sol = GNM_SO_LINE (so);

	gsf_xml_out_add_int (output, "Type", 1);
	write_xml_sax_arrow (&sol->start_arrow, "Start", output);
	write_xml_sax_arrow (&sol->end_arrow, "End", output);

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


static gboolean
read_xml_sax_arrow (xmlChar const **attrs, const char *prefix,
		    GOArrow *arrow)
{
	size_t plen = strlen (prefix);
	const char *attr = CXML2C (attrs[0]);
	const char *val = CXML2C (attrs[1]);

	if (strncmp (attr, prefix, plen) != 0)
		return FALSE;
	attr += plen;

	if (strcmp (attr, "ArrowType") == 0) {
		arrow->typ = go_arrow_type_from_str (val);
	} else if (strcmp (attr, "ArrowShapeA") == 0) {
		arrow->a = go_strtod (val, NULL);
	} else if (strcmp (attr, "ArrowShapeB") == 0) {
		arrow->b = go_strtod (val, NULL);
	} else if (strcmp (attr, "ArrowShapeC") == 0) {
		arrow->c = go_strtod (val, NULL);
	} else
		return FALSE;

	return TRUE;
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
	gboolean old_format = FALSE;
	double tmp, arrow_a = -1., arrow_b = -1., arrow_c = -1.;
	int type = 0;

	if (NULL == doc) {
		doc = gsf_xml_in_doc_new (dtd, NULL);
		gnm_xml_in_doc_dispose_on_exit (&doc);
	}
	gsf_xml_in_push_state (xin, doc, NULL, NULL, attrs);

	go_arrow_clear (&sol->start_arrow);
	go_arrow_clear (&sol->end_arrow);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		/* Old 1.0 and 1.2 */
		if (gnm_xml_attr_double (attrs, "Width", &tmp)) {
			old_format = TRUE;
			sol->style->line.width = tmp;
		} else if (attr_eq (attrs[0], "FillColor")) {
			go_color_from_str (CXML2C (attrs[1]), &sol->style->line.color);
			old_format = TRUE;
		} else if (gnm_xml_attr_int (attrs, "Type", &type)) {
		} else if (gnm_xml_attr_double (attrs, "ArrowShapeA", &arrow_a) ||
			   gnm_xml_attr_double (attrs, "ArrowShapeB", &arrow_b) ||
			   gnm_xml_attr_double (attrs, "ArrowShapeC", &arrow_c))
			old_format = TRUE;
		else if (read_xml_sax_arrow (attrs, "Start", &sol->start_arrow) ||
			 read_xml_sax_arrow (attrs, "End", &sol->end_arrow))
			; /* Nothing */
	}

	/* 2 == arrow */
	if (old_format &&
	    type == 2 &&
	    arrow_a >= 0. && arrow_b >= 0. && arrow_c >= 0.)
		go_arrow_init_kite (&sol->end_arrow,
				    arrow_a, arrow_b, arrow_c);
}

static void
gnm_so_line_copy (SheetObject *dst, SheetObject const *src)
{
	GnmSOLine const *sol = GNM_SO_LINE (src);
	GnmSOLine   *new_sol = GNM_SO_LINE (dst);

	g_object_unref (new_sol->style);
	new_sol->style = go_style_dup (sol->style);
	new_sol->start_arrow = sol->start_arrow;
	new_sol->end_arrow = sol->end_arrow;
}

static void
gnm_so_line_set_property (GObject *obj, guint param_id,
			  GValue const *value, GParamSpec *pspec)
{
	GnmSOLine *sol = GNM_SO_LINE (obj);
	switch (param_id) {
	case SOL_PROP_STYLE: {
		GOStyle *style = go_style_dup (g_value_get_object (value));
		style->interesting_fields = GO_STYLE_LINE;
		g_object_unref (sol->style);
		sol->style = style;
		break;
	}
	case SOL_PROP_START_ARROW:
		sol->start_arrow = *((GOArrow *)g_value_peek_pointer (value));
		break;
	case SOL_PROP_END_ARROW:
		sol->end_arrow = *((GOArrow* )g_value_peek_pointer (value));
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
		g_value_set_boxed (value, &sol->start_arrow);
		break;
	case SOL_PROP_END_ARROW:
		g_value_set_boxed (value, &sol->end_arrow);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
gnm_so_line_finalize (GObject *object)
{
	GnmSOLine *sol = GNM_SO_LINE (object);
	g_clear_object (&sol->style);
	G_OBJECT_CLASS (gnm_so_line_parent_class)->finalize (object);
}

static void
gnm_so_line_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *so_class  = GNM_SO_CLASS (gobject_class);

	gnm_so_line_parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize		= gnm_so_line_finalize;
	gobject_class->set_property	= gnm_so_line_set_property;
	gobject_class->get_property	= gnm_so_line_get_property;
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
                 g_param_spec_boxed ("start-arrow", NULL, NULL,
				     GO_ARROW_TYPE,
				     GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, SOL_PROP_END_ARROW,
                 g_param_spec_boxed ("end-arrow", NULL, NULL,
				     GO_ARROW_TYPE,
				     GSF_PARAM_STATIC | G_PARAM_READWRITE));
}

static void
gnm_so_line_init (GObject *obj)
{
	GnmSOLine *sol  = GNM_SO_LINE (obj);
	sol->style = sol_default_style ();
	go_arrow_clear (&sol->start_arrow);
	go_arrow_clear (&sol->end_arrow);
	GNM_SO (obj)->anchor.base.direction = GOD_ANCHOR_DIR_NONE_MASK;
}

GSF_CLASS (GnmSOLine, gnm_so_line,
	   gnm_so_line_class_init, gnm_so_line_init,
	   GNM_SO_TYPE)
