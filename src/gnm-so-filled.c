/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnm-so-filled.c: Boxes, Ovals and Polygons
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
#include "gnm-so-filled.h"
#include "sheet-object-impl.h"
#include "xml-io.h"

#include <goffice/utils/go-libxml-extras.h>
#include <goffice/graph/gog-style.h>
#include <goffice/utils/go-color.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n.h>
#include <string.h>
#include <math.h>


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

#ifdef WITH_GTK
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
	NULL, NULL,
	FOO_TYPE_CANVAS_GROUP, 0,
	GSF_INTERFACE (so_filled_foo_view_init, SHEET_OBJECT_VIEW_TYPE))
#endif /* WITH_GTK */

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
	res->outline.pattern = 1; /* anything but 0 */
	res->fill.type = GOG_FILL_STYLE_PATTERN;
	go_pattern_set_solid (&res->fill.pattern, RGBA_WHITE);
	return res;
}

/*****************************************************************************/
#ifdef WITH_GTK
#include <sheet-control-gui.h>
#include <dialogs/dialogs.h>
#include <gnumeric-pane.h>
#include <gnumeric-simple-canvas.h>
#include <gnumeric-canvas.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-rect-ellipse.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-util.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-polygon.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-text.h>

static void
gnm_so_filled_user_config (SheetObject *so, SheetControl *sc)
{
	dialog_so_styled (scg_get_wbcg (SHEET_CONTROL_GUI (sc)), G_OBJECT (so),
		GNM_SO_FILLED (so)->style, sof_default_style (),
		_("Filled Object Properties"));
}

static void
cb_gnm_so_filled_style_changed (FooCanvasItem *background, GnmSOFilled const *sof)
{
	GogStyle const *style = sof->style;
	GdkColor outline_buf, *outline_gdk = NULL;
	GdkColor fill_buf, *fill_gdk = NULL;

	if (style->outline.color != 0 &&
	    style->outline.width >= 0 &&
	    style->outline.pattern != 0)
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
		((GnmPane *)container)->gcanvas->object_views,
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

static void
make_rect (GnomePrintContext *gp_context, double x1, double x2, double y1, double y2)
{
	gnome_print_moveto (gp_context, x1, y1);
	gnome_print_lineto (gp_context, x2, y1);
	gnome_print_lineto (gp_context, x2, y2);
	gnome_print_lineto (gp_context, x1, y2);
	gnome_print_lineto (gp_context, x1, y1);
}

/*
 * The following lines are copy and paste from dia. The ellipse logic has been
 * slightly adapted. I have _no_ idea what's going on...
 */

/* This constant is equal to sqrt(2)/3*(8*cos(pi/8) - 4 - 1/sqrt(2)) - 1.
 * Its use should be quite obvious.
 */
#define ELLIPSE_CTRL1 0.26521648984
/* this constant is equal to 1/sqrt(2)*(1-ELLIPSE_CTRL1).
 * Its use should also be quite obvious.
 */
#define ELLIPSE_CTRL2 0.519570402739
#define ELLIPSE_CTRL3 M_SQRT1_2
/* this constant is equal to 1/sqrt(2)*(1+ELLIPSE_CTRL1).
 * Its use should also be quite obvious.
 */
#define ELLIPSE_CTRL4 0.894643159635

static void
make_ellipse (GnomePrintContext *gp_context,
	      double x1, double x2, double y1, double y2)
{
	double width  = x2 - x1;
	double height = y2 - y1;
	double center_x = x1 + width / 2.0;
	double center_y = y1 + height / 2.0;
	double cw1 = ELLIPSE_CTRL1 * width / 2.0;
	double cw2 = ELLIPSE_CTRL2 * width / 2.0;
	double cw3 = ELLIPSE_CTRL3 * width / 2.0;
	double cw4 = ELLIPSE_CTRL4 * width / 2.0;
	double ch1 = ELLIPSE_CTRL1 * height / 2.0;
	double ch2 = ELLIPSE_CTRL2 * height / 2.0;
	double ch3 = ELLIPSE_CTRL3 * height / 2.0;
	double ch4 = ELLIPSE_CTRL4 * height / 2.0;

	gnome_print_moveto (gp_context, x1, center_y);
	gnome_print_curveto (gp_context,
			     x1,             center_y - ch1,
			     center_x - cw4, center_y - ch2,
			     center_x - cw3, center_y - ch3);
	gnome_print_curveto (gp_context,
			     center_x - cw2, center_y - ch4,
			     center_x - cw1, y1,
			     center_x,       y1);
	gnome_print_curveto (gp_context,
			     center_x + cw1, y1,
			     center_x + cw2, center_y - ch4,
			     center_x + cw3, center_y - ch3);
	gnome_print_curveto (gp_context,
			     center_x + cw4, center_y - ch2,
			     x2,             center_y - ch1,
			     x2,             center_y);
	gnome_print_curveto (gp_context,
			     x2,             center_y + ch1,
			     center_x + cw4, center_y + ch2,
			     center_x + cw3, center_y + ch3);
	gnome_print_curveto (gp_context,
			     center_x + cw2, center_y + ch4,
			     center_x + cw1, y2,
			     center_x,       y2);
	gnome_print_curveto (gp_context,
			     center_x - cw1, y2,
			     center_x - cw2, center_y + ch4,
			     center_x - cw3, center_y + ch3);
	gnome_print_curveto (gp_context,
			     center_x - cw4, center_y + ch2,
			     x1,             center_y + ch1,
			     x1,             center_y);
}

static void
set_color (GnomePrintContext *gp_context, GOColor color)
{
	double r = ((double) UINT_RGBA_R (color)) / 255.;
	double g = ((double) UINT_RGBA_G (color)) / 255.;
	double b = ((double) UINT_RGBA_B (color)) / 255.;
	double a = ((double) UINT_RGBA_A (color)) / 255.;
	gnome_print_setrgbcolor (gp_context, r, g, b);
	gnome_print_setopacity (gp_context, a);
}
static void
gnm_so_filled_print (SheetObject const *so, GnomePrintContext *gp_context,
			   double width, double height)
{
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	GogStyle const *style = sof->style;
	GOColor c;

	gnome_print_newpath (gp_context);
	if (sof->is_oval)
		make_ellipse (gp_context, 0., width, 0., -height);
	else
		make_rect (gp_context, 0., width, 0., -height);
	gnome_print_closepath (gp_context);

	if (style->fill.type == GOG_FILL_STYLE_PATTERN &&
	    go_pattern_is_solid (&style->fill.pattern, &c)) {
		gnome_print_gsave (gp_context);
		set_color (gp_context, c);
		gnome_print_fill (gp_context);
		gnome_print_grestore (gp_context);
	}

	if (style->outline.color != 0 &&
	    style->outline.width >= 0 &&
	    style->outline.pattern != 0) {
		gnome_print_setlinewidth (gp_context, style->outline.width);
		set_color (gp_context, style->outline.color);
		gnome_print_stroke (gp_context);
	}
}
#endif /* WITH_GTK */

static gboolean
gnm_so_filled_read_xml_dom (SheetObject *so, char const *typename,
			    XmlParseContext const *ctxt,
			    xmlNodePtr node)
{
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	double	 width;
	char	*label;
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

static gboolean
gnm_so_filled_write_xml_dom (SheetObject const *so,
			     XmlParseContext const *ctxt,
			     xmlNodePtr node)
{
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	xmlNode *child;

	xml_node_set_int (node,  "Type",  sof->is_oval ? 102 : 101);
	xml_node_set_double (node,  "Width", sof->style->outline.width, 2);
	xml_node_set_gocolor (node, "OutlineColor",	sof->style->outline.color);
	xml_node_set_gocolor (node, "FillColor",	sof->style->fill.pattern.back);
	if (sof->text != NULL)
		xml_node_set_cstr (node, "Label", sof->text);

	child = xmlNewDocNode (node->doc, NULL, "Style", NULL);
	gog_persist_dom_save (GOG_PERSIST (sof->style), child);
	xmlAddChild (node, child);
	return FALSE;
}

static void
gnm_so_filled_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	GnmSOFilled const *sof = GNM_SO_FILLED (so);
	gsf_xml_out_add_int	(output, "Type", sof->is_oval ? 102 : 101);
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

	switch (param_id) {
	case SOF_PROP_STYLE:
		g_object_unref (sof->style);
		sof->style = g_object_ref (g_value_get_object (value));
		sof->style->interesting_fields = GOG_STYLE_OUTLINE | GOG_STYLE_FILL;
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
	so_class->write_xml_dom		= gnm_so_filled_write_xml_dom;
	so_class->write_xml_sax		= gnm_so_filled_write_xml_sax;
	so_class->copy			= gnm_so_filled_copy;
	so_class->rubber_band_directly	= TRUE;
	so_class->xml_export_name	= "SheetObjectFilled";

#ifdef WITH_GTK
	so_class->new_view		= gnm_so_filled_new_view;
	so_class->user_config		= gnm_so_filled_user_config;
	so_class->print			= gnm_so_filled_print;
#endif /* WITH_GTK */

        g_object_class_install_property (gobject_class, SOF_PROP_STYLE,
                 g_param_spec_object ("style", NULL, NULL, GOG_STYLE_TYPE,
			(G_PARAM_READABLE | G_PARAM_WRITABLE)));
        g_object_class_install_property (gobject_class, SOF_PROP_IS_OVAL,
                 g_param_spec_boolean ("is-oval", NULL, NULL, FALSE,
			(G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY)));
        g_object_class_install_property (gobject_class, SOF_PROP_MARKUP,
                 g_param_spec_boxed ("markup", NULL, NULL, PANGO_TYPE_ATTR_LIST,
			(G_PARAM_READABLE | G_PARAM_WRITABLE)));
        g_object_class_install_property (gobject_class, SOF_PROP_TEXT,
                 g_param_spec_string ("text", NULL, NULL, NULL,
			(G_PARAM_READABLE | G_PARAM_WRITABLE)));
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
	SHEET_OBJECT (obj)->anchor.direction = SO_DIR_NONE_MASK;
}

GSF_CLASS (GnmSOFilled, gnm_so_filled,
	   gnm_so_filled_class_init, gnm_so_filled_init,
	   SHEET_OBJECT_TYPE);

/************************************************************************/

#define GNM_SO_POLYGON(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SO_POLYGON_TYPE, GnmSOPolygon))
#define GNM_SO_POLYGON_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k),	GNM_SO_POLYGON_TYPE, GnmSOPolygonClass))

typedef struct {
	GnmSOFilled  base;
	GArray *points;
} GnmSOPolygon;
typedef GnmSOFilledClass GnmSOPolygonClass;
static SheetObjectClass *gnm_so_polygon_parent_class;

#ifdef WITH_GTK
#include <goffice/cut-n-paste/foocanvas/foo-canvas.h>
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
		FooCanvasPolygon	*poly = FOO_CANVAS_POLYGON (view);
		SheetObject		*so   = sheet_object_view_get_so (sov);
		GnmSOPolygon const	*sop  = GNM_SO_POLYGON (so);
		double *dst, x_scale, y_scale, x_translate, y_translate;
		double const *src;
		unsigned i = poly->num_points;

		g_return_if_fail (poly->num_points*2 == (int)sop->points->len);

		x_scale = fabs (coords[2] - coords[0]);
		y_scale = fabs (coords[3] - coords[1]);
		x_translate = MIN (coords[0], coords[2]),
		y_translate = MIN (coords[1], coords[3]);

		src = (double *)sop->points->data;
		dst = poly->coords;
		for ( ; i-- > 0; dst += 2, src += 2) {
			dst[0] = x_translate + x_scale * src[0];
			dst[1] = y_translate + y_scale * src[1];
		}

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
	NULL, NULL,
	FOO_TYPE_CANVAS_POLYGON, 0,
	GSF_INTERFACE (so_polygon_foo_view_init, SHEET_OBJECT_VIEW_TYPE))
#endif /* WITH_GTK */

/*****************************************************************************/

#ifdef WITH_GTK
static SheetObjectView *
gnm_so_polygon_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmCanvas *gcanvas = ((GnmPane *)container)->gcanvas;
	GnmSOPolygon *sop = GNM_SO_POLYGON (so);
	FooCanvasItem *item = foo_canvas_item_new (gcanvas->object_views,
		so_polygon_foo_view_get_type (),
		"points",		sop->points,
		/* "join_style",	GDK_JOIN_ROUND, */
		NULL);
#if 0
	cb_gnm_so_filled_style_changed (item, &sop->base);
	g_signal_connect_object (sop,
		"notify", G_CALLBACK (cb_gnm_so_filled_style_changed),
		item, 0);
#endif
	return gnm_pane_object_register (so, item, TRUE);
}

static void
gnm_so_polygon_print (SheetObject const *so, GnomePrintContext *gp_context,
		      double base_x, double base_y)
{
}
#endif /* WITH_GTK */

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
		    !strcmp (ptr->name, "Point") &&
		    xml_node_get_double	(ptr, "x", vals + 0) &&
		    xml_node_get_double	(ptr, "y", vals + 1))
			g_array_append_vals (sop->points, vals, 2);

	return gnm_so_polygon_parent_class->
		read_xml_dom (so, typename, ctxt, node);
}

static gboolean
gnm_so_polygon_write_xml_dom (SheetObject const *so,
			      XmlParseContext const *ctxt, xmlNodePtr node)
{
	GnmSOPolygon *sop = GNM_SO_POLYGON (so);
	return gnm_so_polygon_parent_class->write_xml_dom (so, ctxt, node);
}

static void
gnm_so_polygon_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	GnmSOPolygon const *sop = GNM_SO_POLYGON (so);
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
gnm_so_polygon_finalize (GObject *object)
{
	GnmSOPolygon *sop = GNM_SO_POLYGON (object);

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
	so_class->read_xml_dom		= gnm_so_polygon_read_xml_dom;
	so_class->write_xml_dom		= gnm_so_polygon_write_xml_dom;
	so_class->write_xml_sax		= gnm_so_polygon_write_xml_sax;
	so_class->copy			= gnm_so_polygon_copy;
	so_class->rubber_band_directly	= FALSE;
	so_class->xml_export_name	= "SheetObjectPolygon";

#ifdef WITH_GTK
	so_class->new_view		= gnm_so_polygon_new_view;
	so_class->print			= gnm_so_polygon_print;
#endif /* WITH_GTK */
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
	g_array_append_vals (sop->points,
		initial_coords, G_N_ELEMENTS (initial_coords));
}

GSF_CLASS (GnmSOPolygon, gnm_so_polygon,
	   gnm_so_polygon_class_init, gnm_so_polygon_init,
	   SHEET_OBJECT_TYPE);
