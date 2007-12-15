/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnm-so-filled.c: Boxes, Ovals and Polygons
 *
 * Copyright (C) 2004-2006 Jody Goldberg (jody@gnome.org)
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
#include "gnm-so-filled.h"
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

#define GNM_SO_FILLED(o)	(G_TYPE_CHECK_INSTANCE_CAST((o), GNM_SO_FILLED_TYPE, GnmSOFilled))
#define GNM_SO_FILLED_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST ((k),   GNM_SO_FILLED_TYPE, GnmSOFilledClass))

typedef struct {
	SheetObject base;

	GogStyle  *style;
	gboolean   is_oval;

	char *text;
	/* Only valid if text != NULL && !is_oval */
	PangoAttrList  *markup;
	struct {
		float top, bottom, left, right;
	} margin_pts;
} GnmSOFilled;
typedef SheetObjectClass GnmSOFilledClass;

#ifdef GNM_WITH_GTK
#include <goffice/cut-n-paste/foocanvas/foo-canvas.h>
static void
so_filled_view_destroy (SheetObjectView *sov)
{
	gtk_object_destroy (GTK_OBJECT (sov));
}
static void
so_filled_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem  *view = FOO_CANVAS_ITEM (sov);

	if (visible) {
		FooCanvasGroup	*group = FOO_CANVAS_GROUP (sov);
		SheetObject	*so = sheet_object_view_get_so (sov);
		GnmSOFilled	*sof  = GNM_SO_FILLED (so);
		double w = fabs (coords [2] - coords [0]);
		double h = fabs (coords [3] - coords [1]);

		foo_canvas_item_set (FOO_CANVAS_ITEM (group),
			"x", MIN (coords [0], coords [2]),
			"y", MIN (coords [1], coords [3]),
			NULL);

		foo_canvas_item_set (FOO_CANVAS_ITEM (group->item_list->data),
			"x2", w, "y2", h,
			NULL);

		if (sof->text != NULL && group->item_list->next) {
			view = FOO_CANVAS_ITEM (group->item_list->next->data);
			w -= (sof->margin_pts.left + sof->margin_pts.right)
				* view->canvas->pixels_per_unit;
			h -= (sof->margin_pts.top + sof->margin_pts.bottom)
				* view->canvas->pixels_per_unit;

			foo_canvas_item_set (view,
				"clip_height", h,
				"clip_width",  w,
				"wrap_width",  w,

				/* cheap hack to force the attributes to regenerate for
				 * the rare case where the repositioning was caused by
				 * a change in zoom */
				"underline_set", FALSE,
				NULL);
		}

		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}

static void
so_filled_foo_view_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= so_filled_view_destroy;
	sov_iface->set_bounds	= so_filled_view_set_bounds;
}
typedef FooCanvasGroup		FilledFooView;
typedef FooCanvasGroupClass	FilledFooViewClass;
static GSF_CLASS_FULL (FilledFooView, so_filled_foo_view,
	NULL, NULL, NULL, NULL,
	NULL, FOO_TYPE_CANVAS_GROUP, 0,
	GSF_INTERFACE (so_filled_foo_view_init, SHEET_OBJECT_VIEW_TYPE))
#endif /* GNM_WITH_GTK */

/*****************************************************************************/

static SheetObjectClass *gnm_so_filled_parent_class;

enum {
	SOF_PROP_0,
	SOF_PROP_STYLE,
	SOF_PROP_IS_OVAL,
	SOF_PROP_TEXT,
	SOF_PROP_MARKUP
};

static GogStyle *
sof_default_style (void)
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

/*****************************************************************************/
#ifdef GNM_WITH_GTK
#include <sheet-control-gui.h>
#include <dialogs/dialogs.h>
#include <gnumeric-simple-canvas.h>
#include <gnm-pane.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-rect-ellipse.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-util.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-text.h>

static void
gnm_so_filled_user_config (SheetObject *so, SheetControl *sc)
{
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	dialog_so_styled (scg_wbcg (SHEET_CONTROL_GUI (sc)), G_OBJECT (so),
		sof->style, sof_default_style (),
		(sof->text != NULL)
		? _("Label Properties")
		: _("Filled Object Properties"));
}

static void
cb_gnm_so_filled_style_changed (FooCanvasItem *background, GnmSOFilled const *sof)
{
	GogStyle const *style = sof->style;
	GdkColor outline_buf, *outline_gdk = NULL;
	GdkColor fill_buf, *fill_gdk = NULL;

	if (style->outline.color != 0 &&
	    style->outline.width >= 0 &&
	    style->outline.dash_type != GO_LINE_NONE)
		outline_gdk = go_color_to_gdk (style->outline.color, &outline_buf);

	if (style->fill.type != GOG_FILL_STYLE_NONE)
		fill_gdk = go_color_to_gdk (style->fill.pattern.back, &fill_buf);

	if (style->outline.width > 0.)	/* in pts */
		foo_canvas_item_set (background,
			"width-units",		style->outline.width,
			"outline-color-gdk",	outline_gdk,
			"fill-color-gdk",	fill_gdk,
			NULL);
	else /* hairline 1 pixel that ignores zoom */
		foo_canvas_item_set (background,
			"width-pixels",		1,
			"outline-color-gdk",	outline_gdk,
			"fill-color-gdk",	fill_gdk,
			NULL);

}
static void
cb_gnm_so_filled_changed (GnmSOFilled const *sof,
			  G_GNUC_UNUSED GParamSpec *pspec,
			  FooCanvasGroup *group)
{
	cb_gnm_so_filled_style_changed (group->item_list->data, sof);

	if (!sof->is_oval && sof->text != NULL) {
		if (group->item_list->next == NULL)
			foo_canvas_item_new (group, FOO_TYPE_CANVAS_TEXT,
				"anchor",	GTK_ANCHOR_NW,
				"clip",		TRUE,
				"x",		sof->margin_pts.left,
				"y",		sof->margin_pts.top,
				"attributes",	sof->markup,
				NULL);
		foo_canvas_item_set (FOO_CANVAS_ITEM (group->item_list->next->data),
			"text", sof->text,
			NULL);
	} else if (group->item_list->next != NULL)
		g_object_unref (group->item_list->next->data);
}

static SheetObjectView *
gnm_so_filled_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	FooCanvasGroup *group = (FooCanvasGroup *) foo_canvas_item_new (
		gnm_pane_object_group (GNM_PANE (container)),
		so_filled_foo_view_get_type (),
		NULL);

	foo_canvas_item_new (group,
		sof->is_oval ?  FOO_TYPE_CANVAS_ELLIPSE : FOO_TYPE_CANVAS_RECT,
		"x1", 0., "y1", 0.,
		NULL);
	cb_gnm_so_filled_changed (sof, NULL, group);
	g_signal_connect_object (sof,
		"notify", G_CALLBACK (cb_gnm_so_filled_changed),
		group, 0);
	return gnm_pane_object_register (so, FOO_CANVAS_ITEM (group), TRUE);
}

#endif /* GNM_WITH_GTK */

static void
gnm_so_filled_draw_cairo (SheetObject const *so, cairo_t *cr,
			  double width, double height)
{
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	GogStyle const *style = sof->style;
	cairo_pattern_t *pat = NULL;

	cairo_new_path (cr);
	if (sof->is_oval) {
		cairo_save (cr);
		cairo_scale (cr, width, height);
		cairo_arc (cr, .5, .5, .5, 0., 2 * M_PI);
		cairo_restore (cr);
	} else {
		cairo_move_to (cr, 0., 0.);
		cairo_line_to (cr, width, 0.);
		cairo_line_to (cr, width, height);
		cairo_line_to (cr, 0., height);
		cairo_line_to (cr, 0., 0.);
		cairo_close_path (cr);
	}
	/* Fill the shape */
	pat = gog_style_create_cairo_pattern (style, cr);
	if (pat) {
		cairo_set_source (cr, pat);
		cairo_fill_preserve (cr);
		cairo_pattern_destroy (pat);
	}
	/* Draw the line */
	cairo_set_line_width (cr, (style->outline.width)? style->outline.width: 1.);
	cairo_set_source_rgba (cr,
		UINT_RGBA_R(style->outline.color),
		UINT_RGBA_B(style->outline.color),
		UINT_RGBA_G(style->outline.color),
		UINT_RGBA_A(style->outline.color));
	cairo_stroke (cr);
}

static gboolean
gnm_so_filled_read_xml_dom (SheetObject *so, char const *typename,
			    XmlParseContext const *ctxt,
			    xmlNodePtr node)
{
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	double	 width;
	xmlChar	*label;
	int	 type;
	xmlNode	*child;

	if (NULL != (label = xmlGetProp (node, (xmlChar *)"Label"))) {
		g_object_set (G_OBJECT (sof), "text", label, NULL);
		xmlFree (label);
	}

	if (xml_node_get_int (node, "Type", &type))
		sof->is_oval = (type == 102);

	if (NULL != (child = e_xml_get_child_by_name (node, "Style"))) /* new version */
		return !gog_persist_dom_load (GOG_PERSIST (sof->style), child);

	/* Old 1.0 and 1.2 */
	xml_node_get_gocolor (node, "OutlineColor", &sof->style->outline.color);
	xml_node_get_gocolor (node, "FillColor",    &sof->style->fill.pattern.back);
	if (xml_node_get_double  (node, "Width", &width))
		sof->style->outline.width = width;

	return FALSE;
}

static void
gnm_so_filled_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	GnmSOFilled const *sof = GNM_SO_FILLED (so);
	gsf_xml_out_add_int	(output, "Type", sof->is_oval ? 102 : 101);

	/* Old 1.0 and 1.2 */
	gsf_xml_out_add_float   (output, "Width", sof->style->outline.width, 2);
	gnm_xml_out_add_gocolor (output, "OutlineColor", sof->style->outline.color);
	gnm_xml_out_add_gocolor (output, "FillColor",	 sof->style->fill.pattern.back);
	if (sof->text != NULL)
		gsf_xml_out_add_cstr (output, "Label", sof->text);

	gsf_xml_out_start_element (output, "Style");
	gog_persist_sax_save (GOG_PERSIST (sof->style), output);
	gsf_xml_out_end_element (output); /* </Style> */
}

static void
sof_sax_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	SheetObject *so = gnm_xml_in_cur_obj (xin);
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	gog_persist_prep_sax (GOG_PERSIST (sof->style), xin, attrs);
}

static void
gnm_so_filled_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs)
{
	static GsfXMLInNode const dtd[] = {
	  GSF_XML_IN_NODE (STYLE, STYLE, -1, "Style",	GSF_XML_NO_CONTENT, &sof_sax_style, NULL),
	  GSF_XML_IN_NODE_END
	};
	static GsfXMLInDoc *doc = NULL;
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	double tmp;
	int type;

	if (NULL == doc)
		doc = gsf_xml_in_doc_new (dtd, NULL);
	gsf_xml_in_push_state (xin, doc, NULL, NULL, attrs);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_eq (attrs[0], "Label"))
			g_object_set (G_OBJECT (sof), "text", attrs[1], NULL);
		else if (gnm_xml_attr_int     (attrs, "Type", &type))
			sof->is_oval = (type == 102);

		/* Old 1.0 and 1.2 */
		else if (gnm_xml_attr_double  (attrs, "Width", &tmp))
			sof->style->outline.width = tmp;
		else if (attr_eq (attrs[0], "OutlineColor"))
			go_color_from_str (CXML2C (attrs[1]), &sof->style->outline.color);
		else if (attr_eq (attrs[0], "FillColor"))
			go_color_from_str (CXML2C (attrs[1]), &sof->style->fill.pattern.back);
}

static void
gnm_so_filled_copy (SheetObject *dst, SheetObject const *src)
{
	GnmSOFilled const *sof = GNM_SO_FILLED (src);
	GnmSOFilled   *new_sof = GNM_SO_FILLED (dst);

	g_object_unref (new_sof->style);
	new_sof->is_oval = sof->is_oval;
	new_sof->style	 = gog_style_dup (sof->style);
	new_sof->text	 = g_strdup (sof->text);
	new_sof->margin_pts.top    = sof->margin_pts.top  ;
	new_sof->margin_pts.bottom = sof->margin_pts.bottom;
	new_sof->margin_pts.left   = sof->margin_pts.left;
	new_sof->margin_pts.right  = sof->margin_pts.right;
	if (NULL != (new_sof->markup = sof->markup))
		pango_attr_list_ref (new_sof->markup);
}

static void
gnm_so_filled_set_property (GObject *obj, guint param_id,
			    GValue const *value, GParamSpec *pspec)
{
	GnmSOFilled *sof = GNM_SO_FILLED (obj);
	GogStyle *style;

	switch (param_id) {
	case SOF_PROP_STYLE:
		style = sof->style;
		sof->style = g_object_ref (g_value_get_object (value));
		sof->style->interesting_fields = GOG_STYLE_OUTLINE | GOG_STYLE_FILL;
		g_object_unref (style);
		break;
	case SOF_PROP_IS_OVAL:
		sof->is_oval = g_value_get_boolean (value);
		break;
	case SOF_PROP_TEXT:
		g_free (sof->text);
		sof->text = g_strdup (g_value_get_string (value));
		break;
	case SOF_PROP_MARKUP:
		if (sof->markup != NULL)
			pango_attr_list_unref (sof->markup);
		sof->markup = g_value_peek_pointer (value);
		if (sof->markup != NULL)
			pango_attr_list_ref (sof->markup);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}

static void
gnm_so_filled_get_property (GObject *obj, guint param_id,
			    GValue *value, GParamSpec *pspec)
{
	GnmSOFilled  *sof = GNM_SO_FILLED (obj);
	switch (param_id) {
	case SOF_PROP_STYLE:
		g_value_set_object (value, sof->style);
		break;
	case SOF_PROP_IS_OVAL:
		g_value_set_boolean (value, sof->is_oval);
		break;
	case SOF_PROP_TEXT :
		g_value_set_string (value, sof->text);
		break;
	case SOF_PROP_MARKUP :
		g_value_set_boxed (value, sof->markup);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
gnm_so_filled_finalize (GObject *object)
{
	GnmSOFilled *sof = GNM_SO_FILLED (object);

	g_object_unref (sof->style);
	sof->style = NULL;
	g_free (sof->text);
	sof->text = NULL;
	if (NULL != sof->markup) {
		pango_attr_list_unref (sof->markup);
		sof->markup = NULL;
	}

	G_OBJECT_CLASS (gnm_so_filled_parent_class)->finalize (object);
}

static void
gnm_so_filled_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (gobject_class);

	gnm_so_filled_parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize		= gnm_so_filled_finalize;
	gobject_class->set_property	= gnm_so_filled_set_property;
	gobject_class->get_property	= gnm_so_filled_get_property;
	so_class->read_xml_dom		= gnm_so_filled_read_xml_dom;
	so_class->write_xml_sax		= gnm_so_filled_write_xml_sax;
	so_class->prep_sax_parser	= gnm_so_filled_prep_sax_parser;
	so_class->copy			= gnm_so_filled_copy;
	so_class->rubber_band_directly	= TRUE;
	so_class->xml_export_name	= "SheetObjectFilled";

#ifdef GNM_WITH_GTK
	so_class->new_view		= gnm_so_filled_new_view;
	so_class->user_config		= gnm_so_filled_user_config;
#endif /* GNM_WITH_GTK */

	so_class->draw_cairo	= gnm_so_filled_draw_cairo;

        g_object_class_install_property (gobject_class, SOF_PROP_STYLE,
                 g_param_spec_object ("style", NULL, NULL, GOG_STYLE_TYPE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, SOF_PROP_IS_OVAL,
                 g_param_spec_boolean ("is-oval", NULL, NULL, FALSE,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (gobject_class, SOF_PROP_MARKUP,
                 g_param_spec_boxed ("markup", NULL, NULL, PANGO_TYPE_ATTR_LIST,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, SOF_PROP_TEXT,
                 g_param_spec_string ("text", NULL, NULL, NULL,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
}

static void
gnm_so_filled_init (GObject *obj)
{
	GnmSOFilled *sof = GNM_SO_FILLED (obj);
	sof->style = sof_default_style ();
	sof->text = NULL;
	sof->markup = NULL;
	sof->margin_pts.top  = sof->margin_pts.bottom = 3;
	sof->margin_pts.left = sof->margin_pts.right  = 5;
	SHEET_OBJECT (obj)->anchor.base.direction = GOD_ANCHOR_DIR_NONE_MASK;
}

GSF_CLASS (GnmSOFilled, gnm_so_filled,
	   gnm_so_filled_class_init, gnm_so_filled_init,
	   SHEET_OBJECT_TYPE);
