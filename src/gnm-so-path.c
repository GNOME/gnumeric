/*
 * gnm-so-path.c
 *
 * Copyright (C) 2012 Jean Bréfort <jean.brefort@normalesup.org>
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
#include <application.h>
#include <gnm-so-path.h>
#include <sheet-object-impl.h>
#include <sheet.h>
#include <gutils.h>
#include <xml-sax.h>

#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>

#define CXML2C(s) ((char const *)(s))

static inline gboolean
attr_eq (const xmlChar *a, const char *s)
{
	return !strcmp (CXML2C (a), s);
}

#define GNM_SO_PATH(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SO_PATH_TYPE, GnmSOPath))

typedef struct {
	SheetObject base;
	GOStyle *style;
	GOPath	 *path;
	double x_offset, y_offset, width, height;
	GPtrArray *paths, *styles;

	char *text;
	PangoAttrList  *markup;
	struct {
		double top, bottom, left, right;
	} margin_pts;
} GnmSOPath;
typedef SheetObjectClass GnmSOPathClass;

#ifdef GNM_WITH_GTK
#include <gnm-pane.h>

typedef struct {
	SheetObjectView	base;
	GocItem *path, *text;
	GPtrArray *paths;
} GnmSOPathView;

static void
so_path_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	GnmSOPathView *spv = (GnmSOPathView *) sov;

	if (visible) {
		SheetObject		*so   = sheet_object_view_get_so (sov);
		GnmSOPath const	*sop  = GNM_SO_PATH (so);
		GOPath *path;
		double scale, x_scale, y_scale, x, y;
		if ((sop->path == NULL && sop->paths == NULL) || sop->width <=0. || sop->height <=0.)
			return;

		scale = goc_canvas_get_pixels_per_unit (GOC_ITEM (sov)->canvas);
		x_scale = fabs (coords[2] - coords[0]) / sop->width / scale;
		y_scale = fabs (coords[3] - coords[1]) / sop->height / scale;
		x = MIN (coords[0], coords[2]) / scale - sop->x_offset * x_scale;
		y = MIN (coords[1], coords[3]) / scale - sop->y_offset * y_scale;

		if (sop->path != NULL) {
			path = go_path_scale (sop->path, x_scale, y_scale);
			goc_item_set (spv->path, "x", x, "y", y, "path", path, NULL);
			go_path_free (path);
		} else {
			unsigned i;
			for (i = 0; i < sop->paths->len; i++) {
				path = go_path_scale ((GOPath *) g_ptr_array_index (sop->paths, i), x_scale, y_scale);
				goc_item_set (GOC_ITEM (g_ptr_array_index (spv->paths, i)), "x", x, "y", y, "path", path, NULL);
				go_path_free (path);
			}
		}

		if (spv->text != NULL && GOC_ITEM (spv->text)) {
			double x0, y0, x1, y1;
			if (spv->path)
				goc_item_get_bounds (spv->path, &x0, &y0, &x1, &y1);
			else {
				unsigned i;
				double mx, my, Mx, My;
				x0 = y0 = G_MAXDOUBLE;
				x1 = y1 = -G_MAXDOUBLE;
				for (i = 0; i < spv->paths->len; i++) {
					goc_item_get_bounds (GOC_ITEM (g_ptr_array_index (spv->paths, i)), &mx, &my, &Mx, &My);
					if (mx < x0)
						x0 = mx;
					if (my < y0)
						y0 = my;
					if (Mx > x1)
						x1 = Mx;
					if (My > y1)
						y1 = My;
				}
			}
			x1 -= x0 + sop->margin_pts.left + sop->margin_pts.right;
			y1 -= y0 + sop->margin_pts.top + sop->margin_pts.bottom;
			x0 += x1 / 2. + sop->margin_pts.left;
			y0 += y1 / 2. + sop->margin_pts.top;
			x1 = MAX (x1, DBL_MIN);
			y1 = MAX (y1, DBL_MIN);

			goc_item_set (GOC_ITEM (spv->text),
			              "x", x0,
			              "y", y0,
				          "clip-height", y1,
				          "clip-width",  x1,
				          "wrap-width",  x1,
				          NULL);
		}
	} else
		goc_item_hide (GOC_ITEM (sov));
}

static void
so_path_goc_view_class_init (SheetObjectViewClass *sov_klass)
{
	sov_klass->set_bounds	= so_path_view_set_bounds;
}

typedef SheetObjectViewClass	GnmSOPathViewClass;
static GSF_CLASS (GnmSOPathView, so_path_goc_view,
	so_path_goc_view_class_init, NULL,
	GNM_SO_VIEW_TYPE)

#endif

/*****************************************************************************/

static SheetObjectClass *gnm_so_path_parent_class;
enum {
	SOP_PROP_0,
	SOP_PROP_STYLE,
	SOP_PROP_PATH,
	SOP_PROP_TEXT,
	SOP_PROP_MARKUP,
	SOP_PROP_PATHS,
	SOP_PROP_VIEWBOX
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
cb_gnm_so_path_style_changed (GocItem *item, GnmSOPath const *sop)
{
	GOStyle const *style = sop->style;
	goc_item_set (item, "style", style, NULL);
}

static void
gnm_so_path_user_config (SheetObject *so, SheetControl *sc)
{
	GnmSOPath *sop = GNM_SO_PATH (so);
	dialog_so_styled (scg_wbcg (GNM_SCG (sc)), G_OBJECT (sop),
			  sop_default_style (),
			  _("Filled Object Properties"),
			  SO_STYLED_TEXT);
}

static void
cb_gnm_so_path_changed (GnmSOPath const *sop,
			G_GNUC_UNUSED GParamSpec *pspec,
			GnmSOPathView *group)
{
	GPtrArray *children = goc_group_get_children (GOC_GROUP (group));
	unsigned ui;
	for (ui = 0; ui < children->len; ui++) {
		GocItem *item = g_ptr_array_index (children, ui);
		if (GOC_IS_PATH (item))
			cb_gnm_so_path_style_changed (item, sop);
	}
	g_ptr_array_unref (children);

	if (sop->text != NULL && *sop->text != 0) {
		/* set a font, a very bad solution, but will do until we move to GOString */
		PangoFontDescription *desc = pango_font_description_from_string ("Sans 10");
		GOStyle *style;
		if (group->text == NULL) {
			double x0, y0, x1, y1;
			if (group->path)
				goc_item_get_bounds (group->path, &x0, &y0, &x1, &y1);
			else {
				unsigned i;
				double mx, my, Mx, My;
				x0 = y0 = G_MAXDOUBLE;
				x1 = y1 = -G_MAXDOUBLE;
				for (i = 0; i < group->paths->len; i++) {
					goc_item_get_bounds (GOC_ITEM (g_ptr_array_index (group->paths, i)), &mx, &my, &Mx, &My);
					if (mx < x0)
						x0 = mx;
					if (my < y0)
						y0 = my;
					if (Mx > x1)
						x1 = Mx;
					if (My > y1)
						y1 = My;
				}
			}
			x1 -= x0 + sop->margin_pts.left + sop->margin_pts.right;
			y1 -= y0 + sop->margin_pts.top + sop->margin_pts.bottom;
			x0 += x1 / 2. + sop->margin_pts.left;
			y0 += y1 / 2. + sop->margin_pts.top;
			x1 = MAX (x1, DBL_MIN);
			y1 = MAX (y1, DBL_MIN);
			group->text = goc_item_new (GOC_GROUP (group), GOC_TYPE_TEXT,
				"anchor",	GO_ANCHOR_CENTER,
				"clip",		TRUE,
				"x",		x0,
				"y",		y0,
		        "clip-height", y1,
		        "clip-width",  x1,
		        "wrap-width",  x1,
				"attributes",	sop->markup,
				NULL);
		}
		style = go_styled_object_get_style (GO_STYLED_OBJECT (group->text));
		go_style_set_font_desc (style, desc);
		goc_item_set (group->text,
				     "text", sop->text,
				     "attributes",	sop->markup,
				     NULL);
	} else if (group->text != NULL) {
		g_object_unref (group->text);
		group->text = NULL;
	}
}

static SheetObjectView *
gnm_so_path_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmSOPath *sop = GNM_SO_PATH (so);
	GnmSOPathView *item;
	/* FIXME: this is unsafe if the paths change after the view is created,
	 * but this can't occur for now */
	unsigned i;

	if (sop->path == NULL && sop->paths == NULL)
		return NULL;
	item = (GnmSOPathView *) goc_item_new (
	                            gnm_pane_object_group (GNM_PANE (container)),
	                            so_path_goc_view_get_type (),
	                            NULL);
	if (sop->path)
		item->path = goc_item_new (GOC_GROUP (item),
		                           GOC_TYPE_PATH,
		                           "closed", TRUE,
		                           "fill-rule", TRUE,
		                           NULL);
	else {
		item->paths = g_ptr_array_sized_new (sop->paths->len);
		g_ptr_array_set_free_func (item->paths, g_object_unref);
		for (i = 0; i < sop->paths->len; i++)
			g_ptr_array_add (item->paths,
				             goc_item_new (GOC_GROUP (item),
				                           GOC_TYPE_PATH,
				                           "closed", TRUE,
				                           "fill-rule", TRUE,
				                           NULL));
	}
	cb_gnm_so_path_changed (sop, NULL, item);
	g_signal_connect_object (sop,
		"notify::style", G_CALLBACK (cb_gnm_so_path_changed),
		item, 0);
	return gnm_pane_object_register (so, GOC_ITEM (item), TRUE);
}

#endif

static void
gnm_so_path_draw_cairo (SheetObject const *so, cairo_t *cr,
	double width, double height)
{
	GnmSOPath *sop = GNM_SO_PATH (so);
	GOStyle const *style = sop->style;

	cairo_new_path (cr);
	cairo_save (cr);
	cairo_move_to (cr, -sop->x_offset, -sop->y_offset);
	cairo_scale (cr, width / sop->width, height / sop->height);
	go_path_to_cairo (sop->path, GO_PATH_DIRECTION_FORWARD, cr);
	cairo_restore (cr);
	/* Fill the shape */
	cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD); /* might be an option */
	go_style_fill (style, cr, TRUE);
	/* Draw the line */
	if (go_style_set_cairo_line (style, cr))
		cairo_stroke (cr);
	else
		cairo_new_path (cr);
	/* Draw the text. */
	if (sop->text != NULL && *(sop->text) != '\0') {
		PangoLayout *pl = pango_cairo_create_layout (cr);
		double const scale_h = 72. / gnm_app_display_dpi_get (TRUE);
		double const scale_v = 72. / gnm_app_display_dpi_get (FALSE);
		double pl_height = (height - sop->margin_pts.top
				    - sop->margin_pts.bottom) * PANGO_SCALE
				    / scale_v;
		double pl_width = (width - sop->margin_pts.left
				   - sop->margin_pts.right) * PANGO_SCALE
				   / scale_h;
		/* set a font, a very bad solution, but will do until we move to GOString */
		PangoFontDescription *desc = pango_font_description_from_string ("Sans 10");
		PangoRectangle r;
		pango_layout_set_font_description (pl, desc);
		pango_layout_set_text (pl, sop->text, -1);
		pango_layout_set_attributes (pl, sop->markup);
		pango_layout_set_width (pl, pl_width);
		pango_layout_set_height (pl, pl_height);
		cairo_save (cr);
		pango_layout_get_extents (pl, NULL, &r);
		cairo_move_to (cr,
		               (width - r.width / PANGO_SCALE * scale_h) / 2.,
		               (height - r.height / PANGO_SCALE * scale_v) / 2.);
		cairo_scale (cr, scale_h, scale_v);
		cairo_set_source_rgba (cr, GO_COLOR_TO_CAIRO (style->font.color));
		pango_cairo_show_layout (cr, pl);
		cairo_new_path (cr);
		cairo_restore (cr);
		g_object_unref (pl);
	}
}

static void
gnm_so_path_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
			   G_GNUC_UNUSED GnmConventions const *convs)
{
	GnmSOPath const *sop = GNM_SO_PATH (so);
	char *svg;

	if (sop->text != NULL && *(sop->text) != '\0') {
		gsf_xml_out_add_cstr (output, "Label", sop->text);
		if (sop->markup != NULL) {
			GOFormat *fmt = go_format_new_markup	(sop->markup, TRUE);
			gsf_xml_out_add_cstr (output, "LabelFormat",
					      go_format_as_XL (fmt));
			go_format_unref (fmt);
		}
	}
	if (sop->path) {
		svg = go_path_to_svg (sop->path);
		gsf_xml_out_add_cstr (output, "Path", svg);
		g_free (svg);
	} else if (sop->paths) {
		unsigned i;
		for (i = 0; i < sop->paths->len; i++) {
			gsf_xml_out_start_element (output, "Path");
			svg = go_path_to_svg ((GOPath *) g_ptr_array_index (sop->paths, i));
			gsf_xml_out_add_cstr (output, "Path", svg);
			g_free (svg);
			gsf_xml_out_end_element (output); /* </Path> */
		}
	}

	gsf_xml_out_start_element (output, "Style");
	go_persist_sax_save (GO_PERSIST (sop->style), output);
	gsf_xml_out_end_element (output); /* </Style> */
}

static void
sop_sax_path (GsfXMLIn *xin, xmlChar const **attrs)
{
	SheetObject *so = gnm_xml_in_cur_obj (xin);
	GnmSOPath *sop = GNM_SO_PATH (so);
	GOPath *path;
	g_return_if_fail (sop->path == NULL);
	if (sop->paths == NULL)
		sop->paths = g_ptr_array_new_with_free_func ((GDestroyNotify) go_path_free);
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_eq (attrs[0], "Path")) {
			path = go_path_new_from_svg (attrs[1]);
			if (path)
				g_ptr_array_add (sop->paths, path);
			/* we need to update the extents, not optimal */
			g_ptr_array_ref (sop->paths);
			g_object_set (G_OBJECT (sop), "paths", sop->paths, NULL);
			g_ptr_array_unref (sop->paths);
			return;
		}
}

static void
sop_sax_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	SheetObject *so = gnm_xml_in_cur_obj (xin);
	GnmSOPath *sop = GNM_SO_PATH (so);
	go_persist_prep_sax (GO_PERSIST (sop->style), xin, attrs);
}

static void
gnm_so_path_prep_sax_parser (SheetObject *so, GsfXMLIn *xin,
			       xmlChar const **attrs,
			       G_GNUC_UNUSED GnmConventions const *convs)
{
	static GsfXMLInNode const dtd[] = {
	  GSF_XML_IN_NODE (SOPATH, SOPATH, -1, "SheetObjectPath",	GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (SOPATH, PATH, -1, "Path",	GSF_XML_NO_CONTENT, &sop_sax_path, NULL),
	  GSF_XML_IN_NODE (SOPATH, STYLE, -1, "Style",	GSF_XML_NO_CONTENT, &sop_sax_style, NULL),
	  GSF_XML_IN_NODE_END
	};
	static GsfXMLInDoc *doc = NULL;
	GnmSOPath *sop = GNM_SO_PATH(so);

	if (NULL == doc) {
		doc = gsf_xml_in_doc_new (dtd, NULL);
		gnm_xml_in_doc_dispose_on_exit (&doc);
	}
	gsf_xml_in_push_state (xin, doc, NULL, NULL, attrs);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_eq (attrs[0], "Label"))
			g_object_set (G_OBJECT (sop), "text", attrs[1], NULL);
		else if (attr_eq (attrs[0], "LabelFormat")) {
			GOFormat * fmt = go_format_new_from_XL (attrs[1]);
			if (go_format_is_markup (fmt))
				g_object_set (G_OBJECT (sop),
					      "markup", go_format_get_markup (fmt),
					      NULL);
			go_format_unref (fmt);
		} else if (attr_eq (attrs[0], "Path")) {
			GOPath *path = go_path_new_from_svg (attrs[1]);
			if (path) {
				g_object_set (G_OBJECT (sop), "path", path, NULL);
				go_path_free (path);
			}
		}
}

static void
gnm_so_path_copy (SheetObject *dst, SheetObject const *src)
{
	GnmSOPath const *sop = GNM_SO_PATH (src);
	GnmSOPath   *new_sop = GNM_SO_PATH (dst);

	g_object_unref (new_sop->style);
	new_sop->style = go_style_dup (sop->style);
	new_sop->x_offset = sop->x_offset;
	new_sop->y_offset = sop->y_offset;
	new_sop->width = sop->width;
	new_sop->height = sop->height;
	if (new_sop->path) {
		go_path_free (new_sop->path);
		new_sop->path = NULL;
	} else if (new_sop->paths) {
		g_ptr_array_unref (new_sop->paths);
		new_sop->paths = NULL;
	}
	if (sop->path)
		new_sop->path = go_path_ref (sop->path);
	else {
		unsigned i;
		new_sop->paths = g_ptr_array_new_full (sop->paths->len,
		                                       (GDestroyNotify) go_path_free);
		for (i = 0; i < sop->paths->len; i++)
			g_ptr_array_add (new_sop->paths, go_path_ref (g_ptr_array_index (sop->paths, i)));
	}
	gnm_so_path_parent_class->copy (dst, src);
}

static void
gnm_so_path_set_property (GObject *obj, guint param_id,
			     GValue const *value, GParamSpec *pspec)
{
	GnmSOPath *sop = GNM_SO_PATH (obj);

	switch (param_id) {
	case SOP_PROP_STYLE: {
		GOStyle *style = go_style_dup (g_value_get_object (value));
		style->interesting_fields = GO_STYLE_OUTLINE | GO_STYLE_FILL;
		g_object_unref (sop->style);
		sop->style = style;
		break;
	}
	case SOP_PROP_PATH: {
		GOPath *path = g_value_get_boxed (value);
		if (sop->path)
			go_path_free (sop->path);
		else if (sop->paths)
			g_ptr_array_unref (sop->paths);
		sop->path = NULL;
		sop->paths = NULL;
		if (path) {
			cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 1, 1);
			cairo_t *cr = cairo_create (surface);

			sop->path = go_path_ref (path);
			/* evaluates the bounding rectangle */
			go_path_to_cairo (path, GO_PATH_DIRECTION_FORWARD, cr);
			cairo_fill_extents (cr,
			                    &sop->x_offset, &sop->y_offset,
			                    &sop->width, &sop->height);
			sop->width -= sop->x_offset;
			sop->height -= sop->y_offset;
			cairo_destroy (cr);
			cairo_surface_destroy (surface);
		}
		break;
	}
	case SOP_PROP_PATHS: {
		GPtrArray *paths = g_value_get_boxed (value);
		unsigned i;
		for (i = 0; i < paths->len; i++)
			/* we can only check that the path is not NULL */
			g_return_if_fail (g_ptr_array_index (paths, i) != NULL);
		if (sop->path)
			go_path_free (sop->path);
		else if (sop->paths)
			g_ptr_array_unref (sop->paths);
		sop->path = NULL;
		sop->paths = NULL;
		if (paths) {
			cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 1, 1);
			cairo_t *cr = cairo_create (surface);

			sop->paths = g_ptr_array_ref (paths);
			/* evaluates the bounding rectangle */
			for (i = 0; i < paths->len; i++)
				go_path_to_cairo ((GOPath *) g_ptr_array_index (paths, i),
				                  GO_PATH_DIRECTION_FORWARD, cr);
			cairo_fill_extents (cr,
			                    &sop->x_offset, &sop->y_offset,
			                    &sop->width, &sop->height);
			sop->width -= sop->x_offset;
			sop->height -= sop->y_offset;
			cairo_destroy (cr);
			cairo_surface_destroy (surface);
		}
		break;
	}
	case SOP_PROP_TEXT: {
		char const *str = g_value_get_string (value);
		g_free (sop->text);
		sop->text = g_strdup (str == NULL ? "" : str);
		break;
	}
	case SOP_PROP_MARKUP:
		if (sop->markup != NULL)
			pango_attr_list_unref (sop->markup);
		sop->markup = g_value_peek_pointer (value);
		if (sop->markup != NULL)
			pango_attr_list_ref (sop->markup);
		break;

	case SOP_PROP_VIEWBOX:
		/* not settable */
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}

static void
gnm_so_path_get_property (GObject *obj, guint param_id,
			    GValue *value, GParamSpec *pspec)
{
	GnmSOPath *sop = GNM_SO_PATH (obj);
	switch (param_id) {
	case SOP_PROP_STYLE:
		g_value_set_object (value, sop->style);
		break;
	case SOP_PROP_PATH:
		g_value_set_boxed (value, sop->path);
		break;
	case SOP_PROP_PATHS:
		g_value_set_boxed (value, sop->paths);
		break;
	case SOP_PROP_TEXT:
		g_value_set_string (value, sop->text);
		break;
	case SOP_PROP_MARKUP:
		g_value_set_boxed (value, sop->markup);
		break;
	case SOP_PROP_VIEWBOX:
		g_value_take_string
			(value,
			 g_strdup_printf ("%0.0f %0.0f %0.0f %0.0f", sop->x_offset, sop->y_offset,
					  sop->width + sop->x_offset, sop->height + sop->y_offset));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
gnm_so_path_finalize (GObject *object)
{
	GnmSOPath *sop = GNM_SO_PATH (object);

	if (sop->path != NULL)
		go_path_free (sop->path);
	sop->path = NULL;
	if (sop->paths != NULL)
		g_ptr_array_unref (sop->paths);
	sop->paths = NULL;
	g_object_unref (sop->style);
	sop->style = NULL;
	sop->paths = NULL;
	g_free (sop->text);
	sop->text = NULL;
	if (NULL != sop->markup) {
		pango_attr_list_unref (sop->markup);
		sop->markup = NULL;
	}
	G_OBJECT_CLASS (gnm_so_path_parent_class)->finalize (object);
}

static void
gnm_so_path_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *so_class = GNM_SO_CLASS (gobject_class);

	gnm_so_path_parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize		= gnm_so_path_finalize;
	gobject_class->set_property	= gnm_so_path_set_property;
	gobject_class->get_property	= gnm_so_path_get_property;
	so_class->write_xml_sax		= gnm_so_path_write_xml_sax;
	so_class->prep_sax_parser	= gnm_so_path_prep_sax_parser;
	so_class->copy			= gnm_so_path_copy;
	so_class->rubber_band_directly	= FALSE;
	so_class->xml_export_name	= "SheetObjectPath";

#ifdef GNM_WITH_GTK
	so_class->new_view		= gnm_so_path_new_view;
	so_class->user_config	= gnm_so_path_user_config;
#endif /* GNM_WITH_GTK */
	so_class->draw_cairo	= gnm_so_path_draw_cairo;

    g_object_class_install_property (gobject_class, SOP_PROP_PATH,
             g_param_spec_boxed ("path", NULL, NULL, GO_TYPE_PATH,
		GSF_PARAM_STATIC | G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, SOP_PROP_STYLE,
             g_param_spec_object ("style", NULL, NULL, GO_TYPE_STYLE,
		GSF_PARAM_STATIC | G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, SOP_PROP_TEXT,
             g_param_spec_string ("text", NULL, NULL, NULL,
		GSF_PARAM_STATIC | G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, SOP_PROP_MARKUP,
             g_param_spec_boxed ("markup", NULL, NULL, PANGO_TYPE_ATTR_LIST,
		GSF_PARAM_STATIC | G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, SOP_PROP_PATHS,
             g_param_spec_boxed ("paths", NULL, NULL, G_TYPE_PTR_ARRAY,
		GSF_PARAM_STATIC | G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, SOP_PROP_VIEWBOX,
             g_param_spec_string ("viewbox", NULL, NULL, NULL,
		GSF_PARAM_STATIC | G_PARAM_READABLE));
}

static void
gnm_so_path_init (GObject *obj)
{
	GnmSOPath *sop = GNM_SO_PATH (obj);
	sop->style = sop_default_style ();
}

GSF_CLASS (GnmSOPath, gnm_so_path,
	   gnm_so_path_class_init, gnm_so_path_init,
	   GNM_SO_TYPE)
