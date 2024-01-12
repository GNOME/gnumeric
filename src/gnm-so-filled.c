/*
 * gnm-so-filled.c: Boxes, Ovals and Polygons
 *
 * Copyright (C) 2004-2006 Jody Goldberg (jody@gnome.org)
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
#include <gnm-so-filled.h>
#include <sheet-object-impl.h>
#include <sheet.h>
#include <gutils.h>
#include <xml-sax.h>

#include <goffice/goffice.h>
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

	GOStyle  *style;
	gboolean   is_oval;

	char *text;
	PangoAttrList  *markup;
	struct {
		double top, bottom, left, right;
	} margin_pts;
} GnmSOFilled;
typedef SheetObjectClass GnmSOFilledClass;

#ifdef GNM_WITH_GTK
#include <goffice/goffice.h>

typedef struct {
	SheetObjectView	base;
	GocItem *bg, *text;
} FilledItemView;

static void
so_filled_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	GocItem  *view = GOC_ITEM (sov);
	FilledItemView *fiv = (FilledItemView *) sov;
	double scale = goc_canvas_get_pixels_per_unit (view->canvas);

	if (visible) {
		SheetObject	*so = sheet_object_view_get_so (sov);
		GnmSOFilled	*sof  = GNM_SO_FILLED (so);
		double w = fabs (coords [2] - coords [0]) / scale;
		double h = fabs (coords [3] - coords [1]) / scale;

		goc_item_set (view,
			"x", MIN (coords [0], coords [2]) / scale,
			"y", MIN (coords [1], coords [3]) / scale,
			NULL);

		goc_item_set (GOC_ITEM (fiv->bg),
			"width", w, "height", h,
			NULL);


		if (fiv->text != NULL && GOC_IS_ITEM (fiv->text)) {
			w -= (sof->margin_pts.left + sof->margin_pts.right)
				/ scale;
			w = MAX (w, DBL_MIN);

			h -= (sof->margin_pts.top + sof->margin_pts.bottom)
				/ scale;
			h = MAX (h, DBL_MIN);

			if (sof->is_oval)
				goc_item_set (GOC_ITEM (fiv->text),
				              "x", w / 2.,
				              "y", h / 2.,
				              NULL);

			goc_item_set (GOC_ITEM (fiv->text),
				"clip-height", h,
				"clip-width",  w,
				"wrap-width",  w,
				NULL);
		}

		goc_item_show (view);
	} else
		goc_item_hide (view);
}

static void
so_filled_item_view_class_init (SheetObjectViewClass *sov_klass)
{
	sov_klass->set_bounds	= so_filled_view_set_bounds;
}

typedef SheetObjectViewClass	FilledItemViewClass;
static GSF_CLASS (FilledItemView, so_filled_item_view,
	so_filled_item_view_class_init, NULL,
	GNM_SO_VIEW_TYPE)

#endif /* GNM_WITH_GTK */

/*****************************************************************************/

static SheetObjectClass *gnm_so_filled_parent_class;

enum {
	SOF_PROP_0,
	SOF_PROP_STYLE,
	SOF_PROP_IS_OVAL,
	SOF_PROP_TEXT,
	SOF_PROP_MARKUP,
	SOF_PROP_DOCUMENT
};

static GOStyle *
sof_default_style (void)
{
	GOStyle *res = go_style_new ();
	res->interesting_fields = GO_STYLE_OUTLINE | GO_STYLE_FILL;
	res->line.width = 0; /* hairline */
	res->line.color = GO_COLOR_BLACK;
	res->line.dash_type = GO_LINE_SOLID; /* anything but 0 */
	res->fill.type = GO_STYLE_FILL_PATTERN;
	go_pattern_set_solid (&res->fill.pattern, GO_COLOR_WHITE);
	return res;
}

/*****************************************************************************/
#ifdef GNM_WITH_GTK
#include <sheet-control-gui.h>
#include <dialogs/dialogs.h>
#include <gnumeric-simple-canvas.h>
#include <gnm-pane.h>
#include <goffice/canvas/goc-rectangle.h>
#include <goffice/canvas/goc-ellipse.h>
#include <goffice/canvas/goc-text.h>

static void
gnm_so_filled_user_config (SheetObject *so, SheetControl *sc)
{
	dialog_so_styled (scg_wbcg (GNM_SCG (sc)), G_OBJECT (so),
			  sof_default_style (),
			  _("Filled Object Properties"),
			  SO_STYLED_TEXT);
}

static void
cb_gnm_so_filled_style_changed (GocItem *background, GnmSOFilled const *sof)
{
	GOStyle const *style = sof->style;

	goc_item_set (background, "style", style, NULL);

}
static void
cb_gnm_so_filled_changed (GnmSOFilled const *sof,
			  G_GNUC_UNUSED GParamSpec *pspec,
			  FilledItemView *group)
{
	cb_gnm_so_filled_style_changed (GOC_ITEM (group->bg), sof);

	if (sof->text != NULL) {
		/* set a font, a very bad solution, but will do until we move to GOString */
		PangoFontDescription *desc = pango_font_description_from_string ("Sans 10");
		GOStyle *style;
		double w, h;
		double scale = goc_canvas_get_pixels_per_unit (GOC_ITEM (group)->canvas);
		g_object_get (group->bg, "width", &w, "height", &h, NULL);
		w -= (sof->margin_pts.left + sof->margin_pts.right)
			/ scale;
		w = MAX (w, DBL_MIN);

		h -= (sof->margin_pts.top + sof->margin_pts.bottom)
			/ scale;
		h = MAX (h, DBL_MIN);
		if (group->text == NULL) {
			if (sof->is_oval) {
				group->text = goc_item_new (GOC_GROUP (group), GOC_TYPE_TEXT,
					"anchor",	GO_ANCHOR_CENTER,
					"clip",		TRUE,
					"x",		w / 2.,
					"y",		h / 2.,
					"attributes",	sof->markup,
					NULL);
			} else
				group->text = goc_item_new (GOC_GROUP (group), GOC_TYPE_TEXT,
					"anchor",	GO_ANCHOR_NW,
					"clip",		TRUE,
					"x",		sof->margin_pts.left,
					"y",		sof->margin_pts.top,
					"attributes",	sof->markup,
					NULL);
		}
		style = go_styled_object_get_style (GO_STYLED_OBJECT (group->text));
		go_style_set_font_desc (style, desc);
		goc_item_set (group->text,
		              "text", sof->text,
		              "attributes", sof->markup,
		              "clip-height", h,
		              "clip-width",  w,
		              "wrap-width",  w,
		              NULL);
	} else if (group->text != NULL) {
		g_object_unref (group->text);
		group->text = NULL;
	}
}

static SheetObjectView *
gnm_so_filled_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	FilledItemView *group = (FilledItemView *) goc_item_new (
		gnm_pane_object_group (GNM_PANE (container)),
		so_filled_item_view_get_type (),
		NULL);

	group->bg = goc_item_new (GOC_GROUP (group),
		sof->is_oval ?  GOC_TYPE_ELLIPSE : GOC_TYPE_RECTANGLE,
		"x", 0., "y", 0.,
		NULL);
	cb_gnm_so_filled_changed (sof, NULL, group);
	g_signal_connect_object (sof,
		"notify", G_CALLBACK (cb_gnm_so_filled_changed),
		group, 0);
	return gnm_pane_object_register (so, GOC_ITEM (group), TRUE);
}

#endif /* GNM_WITH_GTK */

static void
gnm_so_filled_draw_cairo (SheetObject const *so, cairo_t *cr,
			  double width, double height)
{
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	GOStyle const *style = sof->style;

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
	go_style_fill (style, cr, TRUE);
	/* Draw the line */
	if (go_style_set_cairo_line (style, cr))
		cairo_stroke (cr);
	else
		cairo_new_path (cr);
	/* Draw the text. */
	if (sof->text != NULL && *(sof->text) != '\0') {
		PangoLayout *pl = pango_cairo_create_layout (cr);
		double const scale_h = 72. / gnm_app_display_dpi_get (TRUE);
		double const scale_v = 72. / gnm_app_display_dpi_get (FALSE);
		double pl_height = (height - sof->margin_pts.top
				    - sof->margin_pts.bottom) * PANGO_SCALE
				    / scale_v;
		double pl_width = (width - sof->margin_pts.left
				   - sof->margin_pts.right) * PANGO_SCALE
				   / scale_h;
		/* set a font, a very bad solution, but will do until we move to GOString */
		PangoFontDescription *desc = pango_font_description_from_string ("Sans 10");
		pango_layout_set_font_description (pl, desc);
		pango_layout_set_text (pl, sof->text, -1);
		pango_layout_set_attributes (pl, sof->markup);
		pango_layout_set_width (pl, pl_width);
		pango_layout_set_height (pl, pl_height);
		cairo_save (cr);
		if (sof->is_oval) {
			PangoRectangle r;
			pango_layout_get_extents (pl, NULL, &r);
			cairo_move_to (cr,
			               (width - r.width / PANGO_SCALE * scale_h) / 2.,
			               (height - r.height / PANGO_SCALE * scale_v) / 2.);
		} else
			cairo_move_to (cr, sof->margin_pts.left,
				       sof->margin_pts.top);
		cairo_scale (cr, scale_h, scale_v);
		cairo_set_source_rgba (cr, GO_COLOR_TO_CAIRO (style->font.color));
		pango_cairo_show_layout (cr, pl);
		cairo_new_path (cr);
		cairo_restore (cr);
		g_object_unref (pl);
		pango_font_description_free (desc);
	}
}

static void
gnm_so_filled_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
			     GnmConventions const *convs)
{
	GnmSOFilled const *sof = GNM_SO_FILLED (so);
	GOStyle const *style = sof->style;
	gsf_xml_out_add_int (output, "Type", sof->is_oval ? 102 : 101);

	if (sof->text != NULL && sof->text[0] != '\0') {
		gsf_xml_out_add_cstr (output, "Label", sof->text);
		if (sof->markup != NULL) {
			GOFormat *fmt = go_format_new_markup (sof->markup, TRUE);
			// Trouble: an empty markup comes back as "@" which
			// will not parse as markup.  Hack it for now.
			if (go_format_is_markup (fmt))
				gsf_xml_out_add_cstr (output, "LabelFormat",
						      go_format_as_XL (fmt));
			go_format_unref (fmt);
		}
	}

	gsf_xml_out_start_element (output, "Style");
	go_persist_sax_save (GO_PERSIST (style), output);
	gsf_xml_out_end_element (output); /* </Style> */
}

static void
sof_sax_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	SheetObject *so = gnm_xml_in_cur_obj (xin);
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	go_persist_prep_sax (GO_PERSIST (sof->style), xin, attrs);
}

static void
gnm_so_filled_prep_sax_parser (SheetObject *so, GsfXMLIn *xin,
			       xmlChar const **attrs,
			       GnmConventions const *convs)
{
	static GsfXMLInNode const dtd[] = {
	  GSF_XML_IN_NODE (STYLE, STYLE, -1, "Style",	GSF_XML_NO_CONTENT, &sof_sax_style, NULL),
	  GSF_XML_IN_NODE_END
	};
	static GsfXMLInDoc *doc = NULL;
	GnmSOFilled *sof = GNM_SO_FILLED (so);
	double tmp;
	int type;

	if (NULL == doc) {
		doc = gsf_xml_in_doc_new (dtd, NULL);
		gnm_xml_in_doc_dispose_on_exit (&doc);
	}
	gsf_xml_in_push_state (xin, doc, NULL, NULL, attrs);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_eq (attrs[0], "Label"))
			g_object_set (G_OBJECT (sof), "text", attrs[1], NULL);
		else if (attr_eq (attrs[0], "LabelFormat")) {
			GOFormat * fmt = go_format_new_from_XL (attrs[1]);
			if (go_format_is_markup (fmt))
				g_object_set (G_OBJECT (sof),
					      "markup", go_format_get_markup (fmt),
					      NULL);
			go_format_unref (fmt);
		} else if (gnm_xml_attr_int     (attrs, "Type", &type))
			sof->is_oval = (type == 102);

		/* Old 1.0 and 1.2 */
		else if (gnm_xml_attr_double  (attrs, "Width", &tmp))
			sof->style->line.width = tmp;
		else if (attr_eq (attrs[0], "OutlineColor"))
			go_color_from_str (CXML2C (attrs[1]), &sof->style->line.color);
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
	new_sof->style	 = go_style_dup (sof->style);
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
	char const * str;

	switch (param_id) {
	case SOF_PROP_STYLE:  {
		GOStyle *style = go_style_dup (g_value_get_object (value));
		style->interesting_fields = GO_STYLE_OUTLINE | GO_STYLE_FILL;
		g_object_unref (sof->style);
		sof->style = style;
		break;
	}
	case SOF_PROP_IS_OVAL:
		sof->is_oval = g_value_get_boolean (value);
		break;
	case SOF_PROP_TEXT:
		g_free (sof->text);
		str = g_value_get_string (value);
		sof->text = g_strdup (str == NULL ? "" : str);
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
	case SOF_PROP_TEXT:
		g_value_set_string (value, sof->text);
		break;
	case SOF_PROP_MARKUP:
		g_value_set_boxed (value, sof->markup);
		break;
	case SOF_PROP_DOCUMENT:
		g_value_set_object (value, sheet_object_get_sheet (GNM_SO (obj))->workbook);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
gnm_so_filled_finalize (GObject *object)
{
	GnmSOFilled *sof = GNM_SO_FILLED (object);

	g_clear_object (&sof->style);

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
	SheetObjectClass *so_class = GNM_SO_CLASS (gobject_class);

	gnm_so_filled_parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize		= gnm_so_filled_finalize;
	gobject_class->set_property	= gnm_so_filled_set_property;
	gobject_class->get_property	= gnm_so_filled_get_property;
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
		g_param_spec_object ("style", NULL, NULL, GO_TYPE_STYLE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, SOF_PROP_IS_OVAL,
                 g_param_spec_boolean ("is-oval", NULL, NULL, FALSE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (gobject_class, SOF_PROP_TEXT,
                 g_param_spec_string ("text", NULL, NULL, NULL,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, SOF_PROP_MARKUP,
                 g_param_spec_boxed ("markup", NULL, NULL, PANGO_TYPE_ATTR_LIST,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, SOF_PROP_DOCUMENT,
		g_param_spec_object ("document", NULL, NULL, GO_TYPE_DOC,
			GSF_PARAM_STATIC | G_PARAM_READABLE));
}

static void
gnm_so_filled_init (GObject *obj)
{
	GnmSOFilled *sof = GNM_SO_FILLED (obj);
	sof->style = sof_default_style ();
	sof->markup = NULL;
	sof->margin_pts.top  = sof->margin_pts.bottom = 3;
	sof->margin_pts.left = sof->margin_pts.right  = 5;
	GNM_SO (obj)->anchor.base.direction = GOD_ANCHOR_DIR_NONE_MASK;
}

GSF_CLASS (GnmSOFilled, gnm_so_filled,
	   gnm_so_filled_class_init, gnm_so_filled_init,
	   GNM_SO_TYPE)
