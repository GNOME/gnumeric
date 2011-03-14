/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * openoffice-write.c : export OpenOffice OASIS .ods files
 *
 * Copyright (C) 2004-2006 Jody Goldberg (jody@gnome.org)
 *
 * Copyright (C) 2006-2010 Andreas J. Guelzow (aguelzow@pyrshep.ca)
 *
 * Copyright (C) 2005 INdT - Instituto Nokia de Tecnologia
 *               Author: Luciano Wolf (luciano.wolf@indt.org.br)
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

/*****************************************************************************/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <workbook-view.h>
#include <goffice/goffice.h>
#include <gnm-format.h>
#include <workbook.h>
#include <workbook-priv.h> /* Workbook::names */
#include <cell.h>
#include <sheet.h>
#include <print-info.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <sheet-merge.h>
#include <style-color.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <value.h>
#include <ranges.h>
#include <mstyle.h>
#include <input-msg.h>
#include <style-border.h>
#include <validation.h>
#include <validation-combo.h>
#include <hlink.h>
#include <sheet-filter.h>
#include <print-info.h>
#include <parse-util.h>
#include <tools/dao.h>
#include <gutils.h>
#include <style-conditions.h>

#include <sheet-object.h>
#include <sheet-object-graph.h>
#include <sheet-object-cell-comment.h>
#include <sheet-object-image.h>
#include <sheet-object-widget.h>
#include <gnm-so-filled.h>
#include <gnm-so-line.h>
#include <sheet-filter-combo.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-outfile.h>
#include <gsf/gsf-outfile-zip.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-opendoc-utils.h>
#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-meta-names.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#define MANIFEST "manifest:"
#define OFFICE	 "office:"
#define STYLE	 "style:"
#define TABLE	 "table:"
#define TEXT     "text:"
#define DUBLINCORE "dc:"
#define FOSTYLE	 "fo:"
#define NUMBER   "number:"
#define DRAW	 "draw:"
#define CHART	 "chart:"
#define SVG	 "svg:"
#define XLINK	 "xlink:"
#define CONFIG   "config:"
#define FORM     "form:"
#define SCRIPT   "script:"
#define OOO      "ooo:"
#define XML      "xml:"
#define GNMSTYLE "gnm:"  /* We use this for attributes and elements not supported by ODF */

typedef struct {
	GsfXMLOut *xml;
	GsfOutfile *outfile;
	GOIOContext *ioc;
	WorkbookView const *wbv;
	Workbook const	   *wb;
	Sheet const	   *sheet;
	GnmConventions *conv;
	GSList *row_styles;
	GSList *col_styles;
	GHashTable *cell_styles;
	GHashTable *named_cell_styles;
	GHashTable *so_styles;
	GHashTable *xl_styles;
	GHashTable *xl_styles_neg;
	GHashTable *xl_styles_zero;
	GHashTable *xl_styles_conditional;
	GnmStyle *default_style;
	ColRowInfo const *row_default;
	ColRowInfo const *column_default;
	GHashTable *graphs;
	GHashTable *graph_dashes;
	GHashTable *graph_hatches;
	GHashTable *graph_fill_images;
	GHashTable *graph_gradients;
	GHashTable *chart_props_hash;
	GHashTable *arrow_markers;
	GHashTable *images;
	GHashTable *controls;

	gboolean with_extension;
	GOFormat const *time_fmt;
	GOFormat const *date_fmt;
	GOFormat const *date_long_fmt;

	char const *object_name;

	/* for the manifest */
	GSList *fill_image_files; /* image/png */

	float last_progress;
	float graph_progress;
	float sheet_progress;
} GnmOOExport;

typedef struct {
	char *name;
	ColRowInfo const *ci;
} col_row_styles_t;

static struct {
	char const *key;
	char const *url;
} const ns[] = {
	{ "xmlns:office",	"urn:oasis:names:tc:opendocument:xmlns:office:1.0" },
	{ "xmlns:style",	"urn:oasis:names:tc:opendocument:xmlns:style:1.0"},
	{ "xmlns:text",		"urn:oasis:names:tc:opendocument:xmlns:text:1.0" },
	{ "xmlns:table",	"urn:oasis:names:tc:opendocument:xmlns:table:1.0" },
	{ "xmlns:draw",		"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" },
	{ "xmlns:fo",		"urn:oasis:names:tc:opendocument:xmlns:" "xsl-fo-compatible:1.0"},
	{ "xmlns:xlink",	"http://www.w3.org/1999/xlink" },
	{ "xmlns:dc",		"http://purl.org/dc/elements/1.1/" },
	{ "xmlns:meta",		"urn:oasis:names:tc:opendocument:xmlns:meta:1.0" },
	{ "xmlns:number",	"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0" },
	{ "xmlns:svg",		"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" },
	{ "xmlns:chart",	"urn:oasis:names:tc:opendocument:xmlns:chart:1.0" },
	{ "xmlns:dr3d",		"urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0" },
	{ "xmlns:config",       "urn:oasis:names:tc:opendocument:xmlns:config:1.0"},
	{ "xmlns:math",		"http://www.w3.org/1998/Math/MathML" },
	{ "xmlns:form",		"urn:oasis:names:tc:opendocument:xmlns:form:1.0" },
	{ "xmlns:script",	"urn:oasis:names:tc:opendocument:xmlns:script:1.0" },
	{ "xmlns:ooo",		"http://openoffice.org/2004/office" },
	{ "xmlns:ooow",		"http://openoffice.org/2004/writer" },
	{ "xmlns:oooc",		"http://openoffice.org/2004/calc" },
	{ "xmlns:of",		"urn:oasis:names:tc:opendocument:xmlns:of:1.2" },
	{ "xmlns:dom",		"http://www.w3.org/2001/xml-events" },
	{ "xmlns:xforms",	"http://www.w3.org/2002/xforms" },
	{ "xmlns:xsd",		"http://www.w3.org/2001/XMLSchema" },
	{ "xmlns:xsi",		"http://www.w3.org/2001/XMLSchema-instance" },
	{ "xmlns:gnm",		"http://www.gnumeric.org/odf-extension/1.0"},
};

/*****************************************************************************/

static void odf_write_fill_images_info (GOImage *image, char const *name, GnmOOExport *state);
static void odf_write_gradient_info (GOStyle const *style, char const *name, GnmOOExport *state);
static void odf_write_hatch_info (GOPattern *pattern, char const *name, GnmOOExport *state);
static void odf_write_dash_info (char const *name, gpointer data, GnmOOExport *state);
static void odf_write_arrow_marker_info (GOArrow const *arrow, char const *name, GnmOOExport *state);

static void odf_write_gog_style_graphic (GnmOOExport *state, GOStyle const *style);
static void odf_write_gog_style_text (GnmOOExport *state, GOStyle const *style);


/*****************************************************************************/

#define PROGRESS_STEPS 500
static void
odf_update_progress (GnmOOExport *state, float delta)
{
	int old = state->last_progress;
	int new;

	state->last_progress += delta;
	new = state->last_progress;

	if (new != old)
		go_io_value_progress_update (state->ioc, new);
}

/*****************************************************************************/


static void
odf_write_mimetype (GnmOOExport *state, GsfOutput *child)
{
	gsf_output_puts (child, "application/vnd.oasis.opendocument.spreadsheet");
}

/*****************************************************************************/


static void
odf_add_chars_non_white (GnmOOExport *state, char const *text, int len)
{
	char * str;

	g_return_if_fail (len > 0);

	str = g_strndup (text, len);
	gsf_xml_out_add_cstr (state->xml, NULL, str);
	g_free (str);
}

static void
odf_add_chars (GnmOOExport *state, char const *text, int len, gboolean *white_written)
{
	int nw = strcspn(text, " \n\t");

	if (nw >= len) {
		odf_add_chars_non_white (state, text, len);
		*white_written = FALSE;
		return;
	}

	if (nw > 0) {
		odf_add_chars_non_white (state, text, nw);
		text += nw;
		len -= nw;
		*white_written = FALSE;
	}

	switch (*text) {
	case ' ':
	{
		int white = strspn(text, " ");

		if (!*white_written) {
			gsf_xml_out_add_cstr (state->xml, NULL, " ");
			len--;
			white--;
			text++;
			*white_written = TRUE;
		}
		if (white > 0) {
			gsf_xml_out_start_element (state->xml, TEXT "s");
			if (white > 1)
				gsf_xml_out_add_int (state->xml, TEXT "c", white);
			gsf_xml_out_end_element (state->xml);
			len -= white;
			text += white;
		}
	}
	break;
	case '\n':
		gsf_xml_out_start_element (state->xml, TEXT "line-break");
		gsf_xml_out_end_element (state->xml);
		text++;
		len--;
		break;
	case '\t':
		gsf_xml_out_start_element (state->xml, TEXT "tab");
		gsf_xml_out_end_element (state->xml);
		text++;
		len--;
		break;
	default:
		/* This really shouldn't happen */
		g_warning ("How can we get here?");
		break;
	}

	if (len > 0)
		odf_add_chars (state, text, len, white_written);
}

static int
odf_attrs_as_string (GnmOOExport *state, PangoAttribute *a)
{
/* 	PangoColor const *c; */
	int spans = 0;

	switch (a->klass->type) {
	case PANGO_ATTR_FAMILY :
		break; /* ignored */
	case PANGO_ATTR_SIZE :
		break; /* ignored */
	case PANGO_ATTR_RISE:
		if (((PangoAttrInt *)a)->value != 0) {
			gsf_xml_out_start_element (state->xml, TEXT "span");
			gsf_xml_out_add_cstr (state->xml, TEXT "style-name",
					      (((PangoAttrInt *)a)->value < 0)
					      ? "AC-subscript"  : "AC-superscript");
			spans += 1;
		}
		break; /* ignored */
	case PANGO_ATTR_STYLE :
		spans += 1;
		gsf_xml_out_start_element (state->xml, TEXT "span");
		gsf_xml_out_add_cstr (state->xml, TEXT "style-name",
				      (((PangoAttrInt *)a)->value
				       == PANGO_STYLE_ITALIC)
				      ? "AC-italic"  : "AC-roman");
		break;
	case PANGO_ATTR_WEIGHT :
	{
		char * str = g_strdup_printf ("AC-weight%i",
					      ((((PangoAttrInt *)a)->value
						+50)/100)*100);
		spans += 1;
		gsf_xml_out_start_element (state->xml, TEXT "span");
		gsf_xml_out_add_cstr (state->xml, TEXT "style-name", str);
		g_free (str);
	}
	break;
	case PANGO_ATTR_STRIKETHROUGH :
		spans += 1;
		gsf_xml_out_start_element (state->xml, TEXT "span");
		gsf_xml_out_add_cstr (state->xml, TEXT "style-name",
				      ((PangoAttrInt *)a)->value
				      ? "AC-strikethrough-solid"
				      : "AC-strikethrough-none");
		break;
	case PANGO_ATTR_UNDERLINE :
	{
		char const *name = NULL;
		switch (((PangoAttrInt *)a)->value) {
		case PANGO_UNDERLINE_NONE :
			name = "AC-underline-none";
			break;
		case PANGO_UNDERLINE_SINGLE :
			name = "AC-underline-single";
			break;
		case PANGO_UNDERLINE_DOUBLE :
			name = "AC-underline-double";
			break;
		case PANGO_UNDERLINE_LOW :
			name = "AC-underline-low";
			break;
		case PANGO_UNDERLINE_ERROR :
			name = "AC-underline-error";
			break;
		default:
			return spans;
		}
		spans += 1;
		gsf_xml_out_start_element (state->xml, TEXT "span");
		gsf_xml_out_add_cstr (state->xml, TEXT "style-name", name);
	}
	break;
	case PANGO_ATTR_FOREGROUND :
/* 		c = &((PangoAttrColor *)a)->color; */
/* 		g_string_append_printf (accum, "[color=%02xx%02xx%02x", */
/* 			((c->red & 0xff00) >> 8), */
/* 			((c->green & 0xff00) >> 8), */
/* 			((c->blue & 0xff00) >> 8)); */
		break;/* ignored */
	default :
		break; /* ignored */
	}

	return spans;
}

static void
odf_new_markup (GnmOOExport *state, const PangoAttrList *markup, char const *text)
{
	int handled = 0;
	PangoAttrIterator * iter;
	int from, to;
	int len = strlen (text);
	/* Since whitespace at the beginning of a <text:p> will be deleted upon    */
	/* reading, we need to behave as if we have already written whitespace and */
	/* use <text:s> if necessary */
	gboolean white_written = TRUE;


	iter = pango_attr_list_get_iterator ((PangoAttrList *) markup);

	do {
		GSList *list, *l;
		int spans = 0;

		pango_attr_iterator_range (iter, &from, &to);
		to = (to > len) ? len : to;       /* Since "to" can be really big! */
		from = (from > len) ? len : from; /* Since "from" can also be really big! */
		if (from > handled)
			odf_add_chars (state, text + handled, from - handled, &white_written);
		list = pango_attr_iterator_get_attrs (iter);
		for (l = list; l != NULL; l = l->next)
			spans += odf_attrs_as_string (state, l->data);
		g_slist_free (list);
		if (to > from) {
			odf_add_chars (state, text + from, to - from, &white_written);
		}
		while (spans-- > 0)
			gsf_xml_out_end_element (state->xml); /* </text:span> */
		handled = to;
	} while (pango_attr_iterator_next (iter));

	pango_attr_iterator_destroy (iter);

	return;
}


/*****************************************************************************/

static void
odf_add_bool (GsfXMLOut *xml, char const *id, gboolean val)
{
	gsf_xml_out_add_cstr_unchecked (xml, id, val ? "true" : "false");
}

static void
odf_add_angle (GsfXMLOut *xml, char const *id, int val)
{
	if (val == -1)
		val = 90;
	gsf_xml_out_add_int (xml, id, val);
}

static void
odf_add_percent (GsfXMLOut *xml, char const *id, double val)
{
	GString *str = g_string_new (NULL);

	g_string_append_printf (str, "%.2f%%", val * 100.);
	gsf_xml_out_add_cstr_unchecked (xml, id, str->str);
	g_string_free (str, TRUE);
}


static void
odf_add_pt (GsfXMLOut *xml, char const *id, double l)
{
	GString *str = g_string_new (NULL);

	g_string_append_printf (str, "%.2fpt", l);
	gsf_xml_out_add_cstr_unchecked (xml, id, str->str);
	g_string_free (str, TRUE);
}

static char *
odf_go_color_to_string (GOColor color)
{
	return g_strdup_printf ("#%.2x%.2x%.2x",
					 GO_COLOR_UINT_R (color),
					 GO_COLOR_UINT_G (color),
					 GO_COLOR_UINT_B (color));
}

static void
gnm_xml_out_add_hex_color (GsfXMLOut *o, char const *id, GnmColor const *c, int pattern)
{
	g_return_if_fail (c != NULL);

	if (pattern == 0)
		gsf_xml_out_add_cstr_unchecked (o, id, "transparent");
	else {
		char *color;
		color = odf_go_color_to_string (c->go_color);
		gsf_xml_out_add_cstr_unchecked (o, id, color);
		g_free (color);
	}
}

static void
odf_write_plot_style_int (GsfXMLOut *xml, GogObject const *plot,
			  GObjectClass *klass, char const *property,
			  char const *id)
{
	GParamSpec *spec;
	if (NULL != (spec = g_object_class_find_property (klass, property))
	    && spec->value_type == G_TYPE_INT
	    && (G_PARAM_READABLE & spec->flags)) {
		int i;
		g_object_get (G_OBJECT (plot), property, &i, NULL);
		gsf_xml_out_add_int (xml, id, i);
	}
}

static void
odf_write_plot_style_uint (GsfXMLOut *xml, GogObject const *plot,
			  GObjectClass *klass, char const *property,
			  char const *id)
{
	GParamSpec *spec;
	if (NULL != (spec = g_object_class_find_property (klass, property))
	    && spec->value_type == G_TYPE_UINT
	    && (G_PARAM_READABLE & spec->flags)) {
		unsigned int i;
		g_object_get (G_OBJECT (plot), property, &i, NULL);
		gsf_xml_out_add_uint (xml, id, i);
	}
}

static void
odf_write_plot_style_double (GsfXMLOut *xml, GogObject const *plot,
			     GObjectClass *klass, char const *property,
			     char const *id)
{
	GParamSpec *spec;
	if (NULL != (spec = g_object_class_find_property (klass, property))
	    && spec->value_type == G_TYPE_DOUBLE
	    && (G_PARAM_READABLE & spec->flags)) {
		double d;
		g_object_get (G_OBJECT (plot), property, &d, NULL);
		gsf_xml_out_add_float (xml, id, d, -1);
	}
}

static void
odf_write_plot_style_double_percent (GsfXMLOut *xml, GogObject const *plot,
				     GObjectClass *klass, char const *property,
				     char const *id)
{
	GParamSpec *spec;
	if (NULL != (spec = g_object_class_find_property (klass, property))
	    && spec->value_type == G_TYPE_DOUBLE
	    && (G_PARAM_READABLE & spec->flags)) {
		double d;
		g_object_get (G_OBJECT (plot), property, &d, NULL);
		odf_add_percent (xml, id, d);
	}
}

static void
odf_write_plot_style_bool (GsfXMLOut *xml, GogObject const *plot,
			  GObjectClass *klass, char const *property,
			  char const *id)
{
	GParamSpec *spec;
	if (NULL != (spec = g_object_class_find_property (klass, property))
	    && spec->value_type == G_TYPE_BOOLEAN
	    && (G_PARAM_READABLE & spec->flags)) {
		gboolean b;
		g_object_get (G_OBJECT (plot), property, &b, NULL);
		odf_add_bool (xml, id, b);
	}
}

static void
odf_write_plot_style_from_bool (GsfXMLOut *xml, GogObject const *plot,
				GObjectClass *klass, char const *property,
				char const *id,
				char const *t_val, char const *f_val)
{
	GParamSpec *spec;
	if (NULL != (spec = g_object_class_find_property (klass, property))
	    && spec->value_type == G_TYPE_BOOLEAN
	    && (G_PARAM_READABLE & spec->flags)) {
		gboolean b;
		g_object_get (G_OBJECT (plot), property, &b, NULL);
		gsf_xml_out_add_cstr (xml, id, b ? t_val : f_val);
	}
}

static void
odf_start_style (GsfXMLOut *xml, char const *name, char const *family)
{
	gsf_xml_out_start_element (xml, STYLE "style");
	gsf_xml_out_add_cstr_unchecked (xml, STYLE "name", name);
	gsf_xml_out_add_cstr_unchecked (xml, STYLE "family", family);
}
static void
odf_write_table_style (GnmOOExport *state,
		       Sheet const *sheet, char const *name)
{
	odf_start_style (state->xml, name, "table");
	gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "master-page-name", "Default");

	gsf_xml_out_start_element (state->xml, STYLE "table-properties");
	odf_add_bool (state->xml, TABLE "display",
		sheet->visibility == GNM_SHEET_VISIBILITY_VISIBLE);
	gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "writing-mode",
		sheet->text_is_rtl ? "rl-tb" : "lr-tb");
	if (state->with_extension) {
		if (sheet->tab_color && !sheet->tab_color->is_auto) {
			gnm_xml_out_add_hex_color (state->xml, GNMSTYLE "tab-color",
						   sheet->tab_color, 1);
		}
		if (sheet->tab_text_color && !sheet->tab_text_color->is_auto) {
			gnm_xml_out_add_hex_color (state->xml,
						   GNMSTYLE "tab-text-color",
						   sheet->tab_text_color, 1);
		}
	}
	gsf_xml_out_end_element (state->xml); /* </style:table-properties> */

	gsf_xml_out_end_element (state->xml); /* </style:style> */
}

static char *
table_style_name (Sheet const *sheet)
{
	return g_strdup_printf ("ta-%p", sheet);
}

static gchar*
odf_get_gog_style_name (GOStyle const *style, GogObject const *obj)
{
	if (style == NULL)
		return g_strdup_printf ("GOG--%p", obj);
	else
		return g_strdup_printf ("GOG-%p", style);
}

static gchar*
odf_get_gog_style_name_from_obj (GogObject const *obj)
{
	GObjectClass *klass = G_OBJECT_GET_CLASS (G_OBJECT (obj));

	if (NULL != g_object_class_find_property (klass, "style")) {
		GOStyle const *style = NULL;
		gchar *name;
		g_object_get (G_OBJECT (obj), "style", &style, NULL);
		name = odf_get_gog_style_name (style, obj);
		g_object_unref (G_OBJECT (style));
		return name;
	} else
		return odf_get_gog_style_name (NULL, obj);
	return NULL;
}

static const char*
xl_find_format (GnmOOExport *state, GOFormat const *format, int i)
{
	GHashTable *hash;
	char const *xl =  go_format_as_XL(format);
	char const *found;
	const char *prefix;

	switch (i) {
	case 0:
		hash = state->xl_styles;
		prefix = "ND.%i";
		break;
	case 1:
		hash = state->xl_styles_neg;
		prefix = "ND-%i";
		break;
	default:
		hash = state->xl_styles_zero;
		prefix = "ND0%i";
		break;
	}

	found = g_hash_table_lookup (hash, xl);

	if (found == NULL) {
		char *new_found;
		new_found = g_strdup_printf (prefix,
					     g_hash_table_size (hash));
		g_hash_table_insert (hash, g_strdup (xl), new_found);
		found = new_found;
	}
	return found;
}

static const char*
xl_find_conditional_format (GnmOOExport *state, GOFormat const *format)
{
	char const *xl =  go_format_as_XL(format);
	char const *found;
	char *condition;

	found = g_hash_table_lookup (state->xl_styles_conditional, xl);

	if (found == NULL) {
		char *new_found;
		new_found = g_strdup_printf ("NDC-%i",
					     g_hash_table_size (state->xl_styles_conditional));
		g_hash_table_insert (state->xl_styles_conditional, g_strdup (xl), new_found);
		found = new_found;
		xl_find_format (state, format, 0);
		xl_find_format (state, format, 1);
		condition = go_format_odf_style_map (format, 2);
		if (condition != NULL) {
			xl_find_format (state, format, 2);
		g_free (condition);
		}
	}

	return found;
}

static void
odf_write_table_styles (GnmOOExport *state)
{
	int i;

	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet const *sheet = workbook_sheet_by_index (state->wb, i);
		char *name = table_style_name (sheet);
		odf_write_table_style (state, sheet, name);
		g_free (name);
	}
}

static gboolean
odf_match_arrow_markers (GOArrow const *old, GOArrow const *new)
{
	return (old->typ == new->typ &&
		old->a == new->a &&
		old->b == new->b &&
		old->c == new->c);
}

static gchar const*
odf_get_arrow_marker_name (GnmOOExport *state, GOArrow *arrow)
{
	gchar const *name = g_hash_table_lookup (state->arrow_markers,
						 (gpointer) arrow);
	gchar *new_name;
	if (name != NULL)
		return name;

	new_name =  g_strdup_printf ("gnm-arrow-%i-%.2f-%.2f-%.2f-%i",
				     arrow->typ,
				     arrow->a,
				     arrow->b,
				     arrow->c,
				     g_hash_table_size (state->arrow_markers));
	g_hash_table_insert (state->arrow_markers,
			     (gpointer) arrow, new_name);
	return new_name;
}


static char *
odf_write_sheet_object_style (GnmOOExport *state, SheetObject *so)
{
	char *name = g_strdup_printf ("so-g-%p", so);
	GOStyle const *style = NULL;
	GObjectClass *klass = G_OBJECT_GET_CLASS (G_OBJECT (so));
	if (NULL != g_object_class_find_property (klass, "style"))
		g_object_get (G_OBJECT (so), "style", &style, NULL);

	odf_start_style (state->xml, name, "graphic");
	gsf_xml_out_start_element (state->xml, STYLE "graphic-properties");
	odf_write_gog_style_graphic (state, style);
	gsf_xml_out_end_element (state->xml); /* </style:graphic-properties> */
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	odf_write_gog_style_text (state, style);
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	if (style != NULL)
		g_object_unref (G_OBJECT (style));
	return name;
}

static char *
odf_write_sheet_object_line_style (GnmOOExport *state, SheetObject *so)
{
	char *name = g_strdup_printf ("so-g-l-%p", so);
	GOStyle const *style = NULL;
	GOArrow *start = NULL, *end = NULL;
	char const *start_arrow_name = NULL;
	char const *end_arrow_name = NULL;

	g_object_get (G_OBJECT (so),
		      "style", &style,
		      "start-arrow", &start,
		      "end-arrow", &end, NULL);

	if (start != NULL && start->typ !=  GO_ARROW_NONE)
		start_arrow_name = odf_get_arrow_marker_name (state, start);
	else
		g_free (start);
	if (end != NULL && end->typ !=  GO_ARROW_NONE)
		end_arrow_name = odf_get_arrow_marker_name (state, end);
	else
		g_free (end);

	odf_start_style (state->xml, name, "graphic");
	gsf_xml_out_start_element (state->xml, STYLE "graphic-properties");
	if (start_arrow_name != NULL)
		gsf_xml_out_add_cstr (state->xml, DRAW "marker-start", start_arrow_name);
	if (end_arrow_name != NULL)
		gsf_xml_out_add_cstr (state->xml, DRAW "marker-end", end_arrow_name);
	odf_write_gog_style_graphic (state, style);
	gsf_xml_out_end_element (state->xml); /* </style:graphic-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	if (style != NULL)
		g_object_unref (G_OBJECT (style));
	return name;
}

static void
odf_write_sheet_object_styles (GnmOOExport *state)
{
	int i;

	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet const *sheet = workbook_sheet_by_index (state->wb, i);
		GSList *objects = sheet_objects_get (sheet, NULL, GNM_SO_FILLED_TYPE), *l;
		for (l = objects; l != NULL; l = l->next) {
			SheetObject *so = SHEET_OBJECT (l->data);
			char *name = odf_write_sheet_object_style (state, so);
			g_hash_table_replace (state->so_styles, so, name);
		}
		g_slist_free (objects);
		objects = sheet_objects_get (sheet, NULL, GNM_SO_LINE_TYPE);
		for (l = objects; l != NULL; l = l->next) {
			SheetObject *so = SHEET_OBJECT (l->data);
			char *name = odf_write_sheet_object_line_style (state, so);
			g_hash_table_replace (state->so_styles, so, name);
		}
		g_slist_free (objects);
	}
}

static void
odf_write_gog_position (GnmOOExport *state, GogObject const *obj)
{
	gboolean is_position_manual = TRUE;
	gchar *position = NULL, *anchor = NULL;

	if (!state->with_extension)
		return;

	g_object_get (G_OBJECT (obj),
		      "is-position-manual", &is_position_manual,
		      "position", &position,
		      "anchor", &anchor,
		      NULL);
	odf_add_bool (state->xml, GNMSTYLE "is-position-manual", is_position_manual);
	if (is_position_manual) {
		if (position)
			gsf_xml_out_add_cstr (state->xml, GNMSTYLE "position", position);
		if (anchor)
			gsf_xml_out_add_cstr (state->xml, GNMSTYLE "anchor", anchor);
	}

	g_free (position);
	g_free (anchor);
}

static void
odf_write_gog_plot_area_position (GnmOOExport *state, GogObject const *obj)
{
	gboolean is_position_manual = TRUE;
	gchar *position = NULL;

	if (!state->with_extension)
		return;

	g_object_get (G_OBJECT (obj),
		      "is-plot-area-manual", &is_position_manual,
		      "plot-area", &position,
		      NULL);
	odf_add_bool (state->xml, GNMSTYLE "is-position-manual", is_position_manual);
	if (is_position_manual && position)
			gsf_xml_out_add_cstr (state->xml, GNMSTYLE "position", position);

	g_free (position);
}

static char *
odf_get_border_format (GnmBorder   *border)
{
	GString *str = g_string_new (NULL);
	double w = gnm_style_border_get_width (border->line_type);
	GnmColor *color = border->color;
	char const *border_type;

	switch (border->line_type) {
	case GNM_STYLE_BORDER_THIN:
		w = 1.;
		border_type = "solid";
		break;
	case GNM_STYLE_BORDER_MEDIUM:
		border_type = "solid";
		break;
	case GNM_STYLE_BORDER_DASHED:
		border_type = "dashed";
		break;
	case GNM_STYLE_BORDER_DOTTED:
		border_type = "dotted";
		break;
	case GNM_STYLE_BORDER_THICK:
		border_type = "solid";
		break;
	case GNM_STYLE_BORDER_DOUBLE:
		border_type = "double";
		break;
	case GNM_STYLE_BORDER_HAIR:
		w = 0.5;
		border_type = "solid";
		break;
	case GNM_STYLE_BORDER_MEDIUM_DASH:
		border_type = "dashed";
		break;
	case GNM_STYLE_BORDER_DASH_DOT:
		border_type = "dashed";
		break;
	case GNM_STYLE_BORDER_MEDIUM_DASH_DOT:
		border_type = "dashed";
		break;
	case GNM_STYLE_BORDER_DASH_DOT_DOT:
		border_type = "dotted";
		break;
	case GNM_STYLE_BORDER_MEDIUM_DASH_DOT_DOT:
		border_type = "dotted";
		break;
	case GNM_STYLE_BORDER_SLANTED_DASH_DOT:
		border_type = "dotted";
		break;
	case GNM_STYLE_BORDER_NONE:
	default:
		w = 0;
		border_type = "none";
		break;
	}

	w = GO_PT_TO_CM (w);
	g_string_append_printf (str, "%.3fcm ", w);
	g_string_append (str, border_type);
	g_string_append_printf (str, " #%.2x%.2x%.2x",
				GO_COLOR_UINT_R (color->go_color),
				GO_COLOR_UINT_G (color->go_color),
				GO_COLOR_UINT_B (color->go_color));
	return g_string_free (str, FALSE);
}

static char const *
odf_get_gnm_border_format (GnmBorder   *border)
{
	char const *border_type = NULL;

	switch (border->line_type) {
	case GNM_STYLE_BORDER_HAIR:
		border_type = "hair";
		break;
	case GNM_STYLE_BORDER_MEDIUM_DASH:
		border_type = "medium-dash";
		break;
	case GNM_STYLE_BORDER_DASH_DOT:
		border_type = "dash-dot";
		break;
	case GNM_STYLE_BORDER_MEDIUM_DASH_DOT:
		border_type = "medium-dash-dot";
		break;
	case GNM_STYLE_BORDER_DASH_DOT_DOT:
		border_type = "dash-dot-dot";
		break;
	case GNM_STYLE_BORDER_MEDIUM_DASH_DOT_DOT:
		border_type = "medium-dash-dot-dot";
		break;
	case GNM_STYLE_BORDER_SLANTED_DASH_DOT:
		border_type = "slanted-dash-dot";
		break;
	default:
		break;
	}
	return border_type;
}


/* ODF write style                                                                        */
/*                                                                                        */
/* We have to write our style information and map them to ODF expectations                */
/* This is supposed to match how we read the styles again in openoffice-read.c            */
/* Note that we are introducing foreign elemetns as required for round tripping           */


#define BORDERSTYLE(msbw, msbwstr, msbwstr_wth, msbwstr_gnm) if (gnm_style_is_element_set (style, msbw)) { \
	                GnmBorder *border = gnm_style_get_border (style, msbw); \
			char *border_style = odf_get_border_format (border); \
			char const *gnm_border_style = odf_get_gnm_border_format (border); \
			gsf_xml_out_add_cstr_unchecked (state->xml, msbwstr, border_style); \
			g_free (border_style); \
                        if (gnm_border_style != NULL && state->with_extension) \
				gsf_xml_out_add_cstr_unchecked (state->xml, msbwstr_gnm, gnm_border_style); \
                        if (border->line_type == GNM_STYLE_BORDER_DOUBLE) \
			        gsf_xml_out_add_cstr_unchecked (state->xml, msbwstr_wth, "0.03cm 0.03cm 0.03cm "); \
		}

#define UNDERLINESPECS(type, style, width) gsf_xml_out_add_cstr (state->xml, \
						      STYLE "text-underline-type", type); \
				gsf_xml_out_add_cstr (state->xml, \
						      STYLE "text-underline-style", style); \
				gsf_xml_out_add_cstr (state->xml, \
						      STYLE "text-underline-width", width)

static void
odf_write_style_cell_properties (GnmOOExport *state, GnmStyle const *style)
{
	gboolean test1, test2;

	gsf_xml_out_start_element (state->xml, STYLE "table-cell-properties");
/* Background Color */
	if (gnm_style_is_element_set (style, MSTYLE_COLOR_BACK))
		gnm_xml_out_add_hex_color (state->xml, FOSTYLE "background-color",
					   gnm_style_get_back_color (style), gnm_style_get_pattern (style));
/* Borders */
	BORDERSTYLE(MSTYLE_BORDER_TOP,FOSTYLE "border-top", STYLE "border-line-width-top", GNMSTYLE "border-line-style-top");
	BORDERSTYLE(MSTYLE_BORDER_BOTTOM,FOSTYLE "border-bottom", STYLE "border-line-width-bottom", GNMSTYLE "border-line-style-bottom");
	BORDERSTYLE(MSTYLE_BORDER_LEFT,FOSTYLE "border-left", STYLE "border-line-width-left", GNMSTYLE "border-line-style-left");
	BORDERSTYLE(MSTYLE_BORDER_RIGHT,FOSTYLE "border-right", STYLE "border-line-width-right", GNMSTYLE "border-line-style-right");
	BORDERSTYLE(MSTYLE_BORDER_DIAGONAL,STYLE "diagonal-bl-tr", STYLE "diagonal-bl-tr-widths", GNMSTYLE "diagonal-bl-tr-line-style");
	BORDERSTYLE(MSTYLE_BORDER_REV_DIAGONAL,STYLE "diagonal-tl-br",  STYLE "diagonal-tl-br-widths", GNMSTYLE "diagonal-tl-br-line-style");
	/* note that we are at this time not setting any of:
	   fo:padding 18.209,
	   fo:padding-bottom 18.210,
	   fo:padding-left 18.211,
	   fo:padding-right 18.212,
	   fo:padding-top 18.213,
	   style:shadow 18.347,
	*/

/* Vertical Alignment */
	if (gnm_style_is_element_set (style, MSTYLE_ALIGN_V)) {
		GnmVAlign align = gnm_style_get_align_v (style);
		char const *alignment = NULL;
		gboolean gnum_specs = FALSE;
		switch (align) {
		case VALIGN_TOP:
			alignment = "top";
			break;
		case VALIGN_BOTTOM:
			alignment= "bottom";
			break;
		case VALIGN_CENTER:
			alignment = "middle";
			break;
		case VALIGN_JUSTIFY:
		case VALIGN_DISTRIBUTED:
		default:
			alignment = "automatic";
			gnum_specs = TRUE;
			break;
		}
		gsf_xml_out_add_cstr (state->xml, STYLE "vertical-align", alignment);
		if (gnum_specs && state->with_extension)
			gsf_xml_out_add_int (state->xml, GNMSTYLE "GnmVAlign", align);
	}

/* Wrapped Text */
	if (gnm_style_is_element_set (style, MSTYLE_WRAP_TEXT))
		gsf_xml_out_add_cstr (state->xml, FOSTYLE "wrap-option",
				      gnm_style_get_wrap_text (style) ? "wrap" : "no-wrap");

/* Shrink-To-Fit */
	if (gnm_style_is_element_set (style, MSTYLE_SHRINK_TO_FIT))
		odf_add_bool (state->xml,  STYLE "shrink-to-fit",
			      gnm_style_get_shrink_to_fit (style));

/* Text Direction */
	/* Note that fo:direction, style:writing-mode and style:writing-mode-automatic interact. */
	/* style:writing-mode-automatic is set in the paragraph properties below. */
	if (gnm_style_is_element_set (style, MSTYLE_TEXT_DIR)) {
		char const *writing_mode = NULL;
		char const *direction = NULL;
		switch (gnm_style_get_text_dir (style)) {
		case GNM_TEXT_DIR_RTL:
			writing_mode = "rl-tb";
			break;
		case GNM_TEXT_DIR_LTR:
			writing_mode = "lr-tb";
			direction = "ltr";
			break;
		case GNM_TEXT_DIR_CONTEXT:
			writing_mode = "page";
			/* Note that we will be setting style:writing-mode-automatic below */
			break;
		}
		if (get_gsf_odf_version () > 101)
			gsf_xml_out_add_cstr (state->xml, STYLE "writing-mode", writing_mode);
		if (direction != NULL)
			gsf_xml_out_add_cstr (state->xml, FOSTYLE "direction", direction);
		gsf_xml_out_add_cstr (state->xml, STYLE "glyph-orientation-vertical", "auto");
	}

/* Cell Protection */
	test1 = gnm_style_is_element_set (style, MSTYLE_CONTENTS_HIDDEN);
	test2 = gnm_style_is_element_set (style, MSTYLE_CONTENTS_LOCKED);
	if (test1 || test2) {
		    gboolean hidden = test1 && gnm_style_get_contents_hidden (style);
		    gboolean protected = test2 && gnm_style_get_contents_locked (style);
		    char const *label;

		    if (hidden)
			    label = protected ? "hidden-and-protected" : "formula-hidden";
		    else
			    label = protected ? "protected" : "none";
		    gsf_xml_out_add_cstr (state->xml, STYLE "cell-protect", label);
	}

/* Rotation */
	if (gnm_style_is_element_set (style, MSTYLE_ROTATION)) {
		gsf_xml_out_add_cstr (state->xml, STYLE "rotation-align", "none");
		odf_add_angle (state->xml, STYLE "rotation-angle",  gnm_style_get_rotation (style));
	}

/* Print Content */
	odf_add_bool (state->xml,  STYLE "print-content", TRUE);

/* Repeat Content */
	odf_add_bool (state->xml,  STYLE "repeat-content", FALSE);

/* Decimal Places (this is the maximum number of decimal places shown if not otherwise specified.)  */
	/* Only interpreted in a default style. */
	gsf_xml_out_add_int (state->xml, STYLE "decimal-places", 13);

/* Input Messages */
	if (gnm_style_is_element_set (style, MSTYLE_INPUT_MSG) && state->with_extension) {
		GnmInputMsg *msg = gnm_style_get_input_msg (style);
		if (msg != NULL) {
			gsf_xml_out_add_cstr (state->xml, GNMSTYLE "input-title",
					      gnm_input_msg_get_title (msg));

			gsf_xml_out_add_cstr (state->xml, GNMSTYLE "input-msg",
					      gnm_input_msg_get_msg (msg));		}
	}

/* Horizontal Alignment */
	if (gnm_style_is_element_set (style, MSTYLE_ALIGN_H)) {
		GnmHAlign align = gnm_style_get_align_h (style);
		char const *source = NULL;
		switch (align) {
		case HALIGN_LEFT:
		case HALIGN_RIGHT:
		case HALIGN_CENTER:
		case HALIGN_JUSTIFY:
		        source = "fix";
			break;
		case HALIGN_GENERAL:
		case HALIGN_FILL:
		case HALIGN_CENTER_ACROSS_SELECTION:
		case HALIGN_DISTRIBUTED:
		default:
			/* Note that since source is value-type, alignment should be ignored */
                        /*(but isn't by OOo) */
			source = "value-type";
			break;
		}
		gsf_xml_out_add_cstr (state->xml, STYLE "text-align-source", source);
	}

	gsf_xml_out_end_element (state->xml); /* </style:table-cell-properties */

}

static void
odf_write_style_paragraph_properties (GnmOOExport *state, GnmStyle const *style)
{
	gsf_xml_out_start_element (state->xml, STYLE "paragraph-properties");
/* Text Direction */
	/* Note that fo:direction, style:writing-mode and style:writing-mode-automatic interact. */
	/* fo:direction and style:writing-mode may have been set in the cell properties above. */
	if (gnm_style_is_element_set (style, MSTYLE_TEXT_DIR))
		odf_add_bool (state->xml,  STYLE "writing-mode-automatic",
			      (gnm_style_get_text_dir (style) == GNM_TEXT_DIR_CONTEXT));

/* Horizontal Alignment */
	if (gnm_style_is_element_set (style, MSTYLE_ALIGN_H)) {
		GnmHAlign align = gnm_style_get_align_h (style);
		char const *alignment = NULL;
		gboolean gnum_specs = FALSE;
		switch (align) {
		case HALIGN_LEFT:
			alignment = "left";
			break;
		case HALIGN_RIGHT:
			alignment= "right";
			break;
		case HALIGN_CENTER:
			alignment = "center";
			break;
		case HALIGN_JUSTIFY:
			alignment = "justify";
			break;
		case HALIGN_GENERAL:
		case HALIGN_FILL:
		case HALIGN_CENTER_ACROSS_SELECTION:
		case HALIGN_DISTRIBUTED:
		default:
			/* Note that since source is value-type, alignment should be ignored */
                        /*(but isn't by OOo) */
			alignment = "start";
			gnum_specs = TRUE;
			break;
		}
		if (align != HALIGN_GENERAL)
			gsf_xml_out_add_cstr (state->xml, FOSTYLE "text-align", alignment);
		if (gnum_specs && state->with_extension)
			gsf_xml_out_add_int (state->xml, GNMSTYLE "GnmHAlign", align);
	}

/* Text Indent */
	if (gnm_style_is_element_set (style, MSTYLE_INDENT))
		odf_add_pt (state->xml, FOSTYLE "text-indent", gnm_style_get_indent (style));

	gsf_xml_out_end_element (state->xml); /* </style:paragraph-properties */
}



static void
odf_write_style_text_properties (GnmOOExport *state, GnmStyle const *style)
{
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");

/* Hidden */
	if (gnm_style_is_element_set (style, MSTYLE_CONTENTS_HIDDEN)) {
		    char const *label = gnm_style_get_contents_hidden (style) ?
			    "none":"true";
		    gsf_xml_out_add_cstr (state->xml, TEXT "display", label);
	}

/* Font Weight */
	if (gnm_style_is_element_set (style, MSTYLE_FONT_BOLD))
		gsf_xml_out_add_int (state->xml, FOSTYLE "font-weight",
				     gnm_style_get_font_bold (style)
				     ? PANGO_WEIGHT_BOLD
				     : PANGO_WEIGHT_NORMAL);
/* Font Style (Italic vs Roman) */
	if (gnm_style_is_element_set (style, MSTYLE_FONT_ITALIC))
		gsf_xml_out_add_cstr (state->xml, FOSTYLE "font-style",
				      gnm_style_get_font_italic (style)
				      ? "italic" : "normal");
/* Strikethrough */
	if (gnm_style_is_element_set (style, MSTYLE_FONT_STRIKETHROUGH)) {
		if (gnm_style_get_font_strike (style)) {
			gsf_xml_out_add_cstr (state->xml,  STYLE "text-line-through-type", "single");
			gsf_xml_out_add_cstr (state->xml, STYLE "text-line-through-style", "solid");
		} else {
			gsf_xml_out_add_cstr (state->xml,  STYLE "text-line-through-type", "none");
			gsf_xml_out_add_cstr (state->xml, STYLE "text-line-through-style", "none");
		}
	}
/* Underline */
	if (gnm_style_is_element_set (style, MSTYLE_FONT_UNDERLINE))
		switch (gnm_style_get_font_uline (style)) {
		case UNDERLINE_NONE:
			UNDERLINESPECS("none", "none", "auto");
			break;
		case UNDERLINE_SINGLE:
			UNDERLINESPECS("single", "solid", "auto");
			break;
		case UNDERLINE_DOUBLE:
			UNDERLINESPECS("double", "solid", "auto");
			break;
		case UNDERLINE_SINGLE_LOW:
			UNDERLINESPECS("single", "solid", "auto");
			break;
		case UNDERLINE_DOUBLE_LOW:
			UNDERLINESPECS("double", "solid", "auto");
			break;
		}
/* Superscript/Subscript */
	if (gnm_style_is_element_set (style, MSTYLE_FONT_SCRIPT))
		switch (gnm_style_get_font_script (style)) {
		case GO_FONT_SCRIPT_SUB:
			gsf_xml_out_add_cstr (state->xml,
					      STYLE "text-position", "sub 80%");
			break;
		case GO_FONT_SCRIPT_STANDARD:
			gsf_xml_out_add_cstr (state->xml,
					      STYLE "text-position", "0% 100%");
			break;
		case GO_FONT_SCRIPT_SUPER:
			gsf_xml_out_add_cstr (state->xml,
					      STYLE "text-position", "super 80%");
			break;
		}
/* Font Size */
	if (gnm_style_is_element_set (style, MSTYLE_FONT_SIZE))
		odf_add_pt (state->xml, FOSTYLE "font-size",
				     gnm_style_get_font_size (style));
/* Foreground Color */
	if (gnm_style_is_element_set (style, MSTYLE_FONT_COLOR))
		gnm_xml_out_add_hex_color (state->xml, FOSTYLE "color",
					   gnm_style_get_font_color (style), 1);
/* Font Family */
	if (gnm_style_is_element_set (style, MSTYLE_FONT_NAME))
		gsf_xml_out_add_cstr (state->xml, FOSTYLE "font-family",
				      gnm_style_get_font_name (style));


	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
}


static void
odf_write_style_goformat_name (GnmOOExport *state, GOFormat const *gof)
{
	char const *name;

	if ((gof == NULL) || go_format_is_markup (gof))
		return;

	if (go_format_is_general (gof))
		name = "General";
	else if (go_format_is_simple (gof))
		name = xl_find_format (state, gof, 0);
	else
		name = xl_find_conditional_format (state, gof);

	gsf_xml_out_add_cstr (state->xml, STYLE "data-style-name", name);
}

static const char*
odf_find_style (GnmOOExport *state, GnmStyle const *style)
{
	char const *found = g_hash_table_lookup (state->named_cell_styles, style);

	if (found == NULL) {
		found = g_hash_table_lookup (state->cell_styles, style);
	}

	if (found == NULL) {
		g_print ("Could not find style %p\n", style);
		return NULL;
	}

	return found;
}

static void
odf_save_style_map_single_f (GnmOOExport *state, GString *str, GnmExprTop const *texpr)
{
	char *formula;
	GnmParsePos pp;

	parse_pos_init (&pp, WORKBOOK (state->wb), state->sheet, 0, 0);

	formula = gnm_expr_top_as_string (texpr, &pp, state->conv);
	g_string_append (str, formula);
	g_free (formula);
}


static void
odf_save_style_map_double_f (GnmOOExport *state, GString *str, GnmStyleCond const *cond)
{
	g_string_append_c (str, '(');
	odf_save_style_map_single_f (state, str, cond->texpr[0]);
	g_string_append_c (str, ',');
	odf_save_style_map_single_f (state, str, cond->texpr[1]);
	g_string_append_c (str, ')');
}

static void
odf_save_style_map (GnmOOExport *state, GnmStyleCond const *cond)
{
	char const *name = odf_find_style (state, cond->overlay);
	GString *str;

	g_return_if_fail (name != NULL);

	str = g_string_new (NULL);

	switch (cond->op) {
	case GNM_STYLE_COND_BETWEEN:
		g_string_append (str, "cell-content-is-between");
		odf_save_style_map_double_f (state, str, cond);
		break;
	case GNM_STYLE_COND_NOT_BETWEEN:
		g_string_append (str, "cell-content-is-not-between");
		odf_save_style_map_double_f (state, str, cond);
		break;
	case GNM_STYLE_COND_EQUAL:
		g_string_append (str, "cell-content()=");
		odf_save_style_map_single_f (state, str, cond->texpr[0]);
		break;
	case GNM_STYLE_COND_NOT_EQUAL:
		g_string_append (str, "cell-content()!=");
		odf_save_style_map_single_f (state, str, cond->texpr[0]);
		break;
	case GNM_STYLE_COND_GT:
		g_string_append (str, "cell-content()>");
		odf_save_style_map_single_f (state, str, cond->texpr[0]);
		break;
	case GNM_STYLE_COND_LT:
		g_string_append (str, "cell-content()<");
		odf_save_style_map_single_f (state, str, cond->texpr[0]);
		break;
	case GNM_STYLE_COND_GTE:
		g_string_append (str, "cell-content()>=");
		odf_save_style_map_single_f (state, str, cond->texpr[0]);
		break;
	case GNM_STYLE_COND_LTE:
		g_string_append (str, "cell-content()<=");
		odf_save_style_map_single_f (state, str, cond->texpr[0]);
		break;

	case GNM_STYLE_COND_CUSTOM:
	case GNM_STYLE_COND_CONTAINS_STR:
	case GNM_STYLE_COND_NOT_CONTAINS_STR:
	case GNM_STYLE_COND_BEGINS_WITH_STR:
	case GNM_STYLE_COND_NOT_BEGINS_WITH_STR:
	case GNM_STYLE_COND_ENDS_WITH_STR:
	case GNM_STYLE_COND_NOT_ENDS_WITH_STR:
	case GNM_STYLE_COND_CONTAINS_ERR:
	case GNM_STYLE_COND_NOT_CONTAINS_ERR:
	case GNM_STYLE_COND_CONTAINS_BLANKS:
	case GNM_STYLE_COND_NOT_CONTAINS_BLANKS:
	default:
		g_string_free (str, TRUE);
		return;
	}

	gsf_xml_out_start_element (state->xml, STYLE "map");

	gsf_xml_out_add_cstr (state->xml, STYLE "apply-style-name", name);
/* 	gsf_xml_out_add_cstr (state->xml, STYLE "base-cell-address","A1"); */
	gsf_xml_out_add_cstr (state->xml, STYLE "condition", str->str);

	gsf_xml_out_end_element (state->xml); /* </style:map> */

	g_string_free (str, TRUE);

}

static void
odf_write_style (GnmOOExport *state, GnmStyle const *style, gboolean is_default)
{
	GnmStyleConditions const *sc;
	GArray const *conds;
	guint i;

	if ((!is_default) && gnm_style_is_element_set (style, MSTYLE_FORMAT)) {
		GOFormat const *format = gnm_style_get_format(style);
		if (format != NULL)
			odf_write_style_goformat_name (state, format);
	}

	odf_write_style_cell_properties (state, style);
	odf_write_style_paragraph_properties (state, style);
	odf_write_style_text_properties (state, style);

	if (gnm_style_is_element_set (style, MSTYLE_CONDITIONS) &&
	    NULL != (sc = gnm_style_get_conditions (style)) &&
	    NULL != (conds = gnm_style_conditions_details (sc)))
		for (i = 0 ; i < conds->len ; i++)
			odf_save_style_map (state, &g_array_index (conds, GnmStyleCond, i));

/* MSTYLE_VALIDATION validations need to be written at a different place and time in ODF  */
/* MSTYLE_HLINK hyperlinks can not be attached to styles but need to be attached to the cell content */
}

#undef UNDERLINESPECS
#undef BORDERSTYLE

static gint
odf_compare_ci (gconstpointer a, gconstpointer b)
{
	col_row_styles_t const *old_style = a;
	ColRowInfo const *new_style = b;

	return !colrow_equal (new_style, old_style->ci);
}

static void
odf_write_row_style (GnmOOExport *state, ColRowInfo const *ci)
{
	gsf_xml_out_start_element (state->xml, STYLE "table-row-properties");
	odf_add_pt (state->xml, STYLE "row-height", ci->size_pts);
	odf_add_bool (state->xml, STYLE "use-optimal-row-height",
		      !ci->hard_size);
	gsf_xml_out_end_element (state->xml); /* </style:table-column-properties> */
}

static const char*
odf_find_row_style (GnmOOExport *state, ColRowInfo const *ci, gboolean write)
{
	col_row_styles_t *new_style;
	GSList *found = g_slist_find_custom (state->row_styles, ci, odf_compare_ci);

	if (found) {
		new_style = found->data;
		return new_style->name;
	} else {
		if (write) {
			new_style = g_new0 (col_row_styles_t,1);
			new_style->ci = ci;
			new_style->name = g_strdup_printf ("AROW-%i", g_slist_length (state->row_styles));
			state->row_styles = g_slist_prepend (state->row_styles, new_style);
			odf_start_style (state->xml, new_style->name, "table-row");
			if (ci != NULL)
				odf_write_row_style (state, ci);
			gsf_xml_out_end_element (state->xml); /* </style:style> */
			return new_style->name;
		} else {
			g_warning("We forgot to export a required row style!");
			return "Missing-Row-Style";
		}
	}
}

static void
odf_write_col_style (GnmOOExport *state, ColRowInfo const *ci)
{
	gsf_xml_out_start_element (state->xml, STYLE "table-column-properties");
	odf_add_pt (state->xml, STYLE "column-width", ci->size_pts);
	odf_add_bool (state->xml, STYLE "use-optimal-column-width",
		      !ci->hard_size);
	gsf_xml_out_end_element (state->xml); /* </style:table-column-properties> */
}

static const char*
odf_find_col_style (GnmOOExport *state, ColRowInfo const *ci, gboolean write)
{
	col_row_styles_t *new_style;
	GSList *found = g_slist_find_custom (state->col_styles, ci, odf_compare_ci);

	if (found) {
		new_style = found->data;
		return new_style->name;
	} else {
		if (write) {
			new_style = g_new0 (col_row_styles_t,1);
			new_style->ci = ci;
			new_style->name = g_strdup_printf ("ACOL-%i", g_slist_length (state->col_styles));
			state->col_styles = g_slist_prepend (state->col_styles, new_style);
			odf_start_style (state->xml, new_style->name, "table-column");
			if (ci != NULL)
				odf_write_col_style (state, ci);
			gsf_xml_out_end_element (state->xml); /* </style:style> */
			return new_style->name;
		} else {
			g_warning("We forgot to export a required column style!");
			return "Missing-Column-Style";
		}
	}
}

static void
odf_save_this_style_with_name (GnmStyle *style, char const *name, GnmOOExport *state)
{
	odf_start_style (state->xml, name, "table-cell");
	odf_write_style (state, style, FALSE);
	gsf_xml_out_end_element (state->xml); /* </style:style */
}

static void
odf_store_this_named_style (GnmStyle *style, char const *name, GnmOOExport *state)
{
	char *real_name;
	GnmStyleConditions const *sc;

	if (name == NULL) {
		int i = g_hash_table_size (state->named_cell_styles);
                /* All styles referenced by a style:map need to be named, so in that case */
		/* we make up a name, that ought to look nice */
		real_name = g_strdup_printf ("Gnumeric-%i", i);
	} else
		real_name = g_strdup (name);

	g_hash_table_insert (state->named_cell_styles, style, real_name);

	if (gnm_style_is_element_set (style, MSTYLE_CONDITIONS) &&
	    NULL != (sc = gnm_style_get_conditions (style))) {
		GArray const *conds = gnm_style_conditions_details (sc);
		if (conds != NULL) {
			guint i;
			for (i = 0 ; i < conds->len ; i++) {
				GnmStyleCond const *cond;
				cond = &g_array_index (conds, GnmStyleCond, i);
				odf_store_this_named_style (cond->overlay, NULL, state);
			}
		}
	}
}

static void
odf_save_this_style (GnmStyle *style, G_GNUC_UNUSED gconstpointer dummy, GnmOOExport *state)
{
	char *name = g_strdup_printf ("ACE-%p", style);
	GnmStyleConditions const *sc;

	g_hash_table_insert (state->cell_styles, style, name);

	if (gnm_style_is_element_set (style, MSTYLE_CONDITIONS) &&
	    NULL != (sc = gnm_style_get_conditions (style))) {
		GArray const *conds = gnm_style_conditions_details (sc);
		if (conds != NULL) {
			guint i;
			for (i = 0 ; i < conds->len ; i++) {
				GnmStyleCond const *cond;
				cond = &g_array_index (conds, GnmStyleCond, i);
				odf_store_this_named_style (cond->overlay, NULL, state);
			}
		}
	}

	odf_save_this_style_with_name (style, name, state);
}

static void
odf_write_character_styles (GnmOOExport *state)
{
	int i;

	for (i = 100; i < 1000; i+=100) {
		char * str = g_strdup_printf ("AC-weight%i", i);
		odf_start_style (state->xml, str, "text");
		gsf_xml_out_start_element (state->xml, STYLE "text-properties");
		gsf_xml_out_add_int (state->xml, FOSTYLE "font-weight", i);
		gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
		gsf_xml_out_end_element (state->xml); /* </style:style> */
		g_free (str);
	}

	odf_start_style (state->xml, "AC-italic", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, FOSTYLE "font-style", "italic");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "AC-roman", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, FOSTYLE "font-style", "normal");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "AC-subscript", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-position", "sub 75%");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "AC-superscript", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-position", "super 75%");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "AC-strikethrough-solid", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-line-through-type", "single");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-line-through-style", "solid");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "AC-strikethrough-none", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-line-through-type", "none");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-line-through-style", "none");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "AC-underline-none", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-type", "none");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-style", "none");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-width", "auto");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "AC-underline-single", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-type", "single");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-style", "solid");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-width", "auto");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "AC-underline-double", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-type", "double");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-style", "solid");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-width", "auto");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "AC-underline-low", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-type", "single");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-style", "solid");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-width", "bold");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "AC-underline-error", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-type", "single");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-style", "wave");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-underline-width", "auto");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	if (state->row_default != NULL)
		odf_find_row_style (state, state->row_default, TRUE);
	if (state->column_default != NULL)
		odf_find_col_style (state, state->column_default, TRUE);

}

static void
odf_write_cell_styles (GnmOOExport *state)
{
	int i;
	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		state->sheet = workbook_sheet_by_index (state->wb, i);
		sheet_style_foreach (state->sheet,
				     (GHFunc) odf_save_this_style,
				     state);
	}
	state->sheet = NULL;
}

static void
odf_write_column_styles (GnmOOExport *state)
{
       /* We have to figure out which automatic styles we need   */
	/* This is really annoying since we have to scan through  */
	/* all columnss. If we could store these styless in styles.xml */
	/* we could create them as we need them, but these styles */
	/* have to go into the beginning of content.xml.          */
	int j;

	for (j = 0; j < workbook_sheet_count (state->wb); j++) {
		Sheet const *sheet = workbook_sheet_by_index (state->wb, j);
		int max_cols = gnm_sheet_get_max_cols (sheet);
		int i;
		ColRowInfo const *last_ci;

		odf_find_col_style (state, &sheet->cols.default_style, TRUE);

		last_ci = sheet_col_get (sheet, 0);
		odf_find_col_style (state, last_ci , TRUE);

		for (i = 1; i < max_cols; i++) {
			ColRowInfo const *this_ci = sheet_col_get (sheet, i);
			if (!colrow_equal (last_ci, this_ci))
				odf_find_col_style (state, (last_ci = this_ci), TRUE);
		}
	}

	return;
}

static void
odf_write_row_styles (GnmOOExport *state)
{
       /* We have to figure out which automatic styles we need   */
	/* This is really annoying since we have to scan through  */
	/* all rows. If we could store these styless in styles.xml */
	/* we could create them as we need them, but these styles */
	/* have to go into the beginning of content.xml.          */
	int j;

	for (j = 0; j < workbook_sheet_count (state->wb); j++) {
		Sheet const *sheet = workbook_sheet_by_index (state->wb, j);
		int max_rows = gnm_sheet_get_max_rows (sheet);
		int i;
		ColRowInfo const *last_ci;

		odf_find_row_style (state, &sheet->rows.default_style, TRUE);

		last_ci = sheet_row_get (sheet, 0);
		odf_find_row_style (state, last_ci , TRUE);

		for (i = 1; i < max_rows; i++) {
			ColRowInfo const *this_ci = sheet_row_get (sheet, i);
			if (!colrow_equal (last_ci, this_ci))
				odf_find_row_style (state, (last_ci = this_ci), TRUE);
		}
	}

	return;
}

static void
odf_cellref_as_string (GnmConventionsOut *out,
		       GnmCellRef const *cell_ref,
		       gboolean no_sheetname)
{
	g_string_append (out->accum, "[");
	if (cell_ref->sheet == NULL)
		g_string_append_c (out->accum, '.');
	cellref_as_string (out, cell_ref, FALSE);
	g_string_append (out->accum, "]");
}

#warning Check on external ref syntax

static void
odf_rangeref_as_string (GnmConventionsOut *out, GnmRangeRef const *ref)
{
	g_string_append (out->accum, "[");
	if (ref->a.sheet == NULL)
		g_string_append_c (out->accum, '.');
	cellref_as_string (out, &(ref->a), FALSE);

	if (ref->b.sheet == NULL)
		g_string_append (out->accum, ":.");
	else
		g_string_append_c (out->accum, ':');

	cellref_as_string (out, &(ref->b), FALSE);
	g_string_append (out->accum, "]");
}

static gboolean
odf_func_r_dchisq_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	if (func->argc == 2) {
		GString *target = out->accum;
		GnmExprConstPtr const *ptr = func->argv;
		g_string_append (target, "CHISQDIST(");
		gnm_expr_as_gstring (ptr[0], out);
		g_string_append_c (out->accum, ';');
		gnm_expr_as_gstring (ptr[1], out);
		g_string_append (out->accum, ";FALSE())");
		return TRUE;
	}
	return FALSE;
}

static gboolean
odf_func_r_pchisq_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	if (func->argc == 2) {
		GString *target = out->accum;
		g_string_append (target, "CHISQDIST");
		gnm_expr_list_as_string (func->argc, func->argv, out);
		return TRUE;
	}
	return FALSE;
}

static gboolean
odf_func_r_qchisq_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	if (func->argc == 2) {
		GString *target = out->accum;
		g_string_append (target, "CHISQINV");
		gnm_expr_list_as_string (func->argc, func->argv, out);
		return TRUE;
	}
	return FALSE;
}

static gboolean
odf_func_floor_ceiling_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	GString *target = out->accum;
	GnmExprConstPtr const *ptr = func->argv;
	g_string_append (target, func->func->name);
	g_string_append_c (target, '(');
	if (func->argc > 0) {
		gnm_expr_as_gstring (ptr[0], out);
		g_string_append_c (target, ';');
		if (func->argc > 1)
			gnm_expr_as_gstring (ptr[1], out);
		else {
			g_string_append (target, "SIGN(");
			gnm_expr_as_gstring (ptr[0], out);
			g_string_append_c (target, ')');
		}
		g_string_append (target, ";1)");
	} else {
		g_string_append (target, func->func->name);
		g_string_append (target, "()");
	}
	return TRUE;
}

static gboolean
odf_func_sec_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	GString *target = out->accum;
	g_string_append (target, "(1/COS");
	gnm_expr_list_as_string (func->argc, func->argv, out);
	g_string_append_c (target, ')');
	return TRUE;
}

static gboolean
odf_func_sech_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	GString *target = out->accum;
	g_string_append (target, "(1/COSH");
	gnm_expr_list_as_string (func->argc, func->argv, out);
	g_string_append_c (target, ')');
	return TRUE;
}


static gboolean
odf_func_eastersunday_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	if (func->argc == 1) {
		GString *target = out->accum;
		GnmExprConstPtr const *ptr = func->argv;
/* OOo incorrectly stores this without an ORG.OPENOFFICE. prefix. */
		g_string_append (target, "EASTERSUNDAY(");
		gnm_expr_as_gstring (ptr[0], out);
		g_string_append (out->accum, ")");
		return TRUE;
	}
	return FALSE;
}

static void
odf_expr_func_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	static struct {
		char const *gnm_name;
		gpointer handler;
	} const sc_func_handlers[] = {
			{"CEILING",  odf_func_floor_ceiling_handler},
			{"FLOOR",    odf_func_floor_ceiling_handler},
			{"R.QCHISQ", odf_func_r_qchisq_handler},
			{"R.DCHISQ", odf_func_r_dchisq_handler},
			{"R.PCHISQ", odf_func_r_pchisq_handler},
			{"SEC",      odf_func_sec_handler},
			{"SECH",      odf_func_sech_handler},
			{"EASTERSUNDAY", odf_func_eastersunday_handler},
			{NULL, NULL}
	};

	static struct {
		char const *gnm_name;
		char const *odf_name;
	} const sc_func_renames[] = {

		/* The default behaviour is to precede each function name with            */
		/* ORG.GNUMERIC. So we need not list gnumeric unique names or those that  */
		/* come from unknown plugins                                              */

		/* The following are functions that exist in OpenFormula, this listing is */
		/* alphabetical by the second entry, the OpenFormula name.                */

		{ "ABS","ABS" },
		{ "ACCRINT","ACCRINT" },
		{ "ACCRINTM","ACCRINTM" },
		{ "ACOS","ACOS" },
		{ "ACOSH","ACOSH" },
		{ "ACOT","ACOT" },
		{ "ACOTH","ACOTH" },
		{ "ADDRESS","ADDRESS" },
		{ "AMORDEGRC","AMORDEGRC" },
		{ "AMORLINC","AMORLINC" },
		{ "AND","AND" },
		{ "ARABIC","ARABIC" },
		{ "AREAS","AREAS" },
		{ "ASC","ASC" },
		{ "ASIN","ASIN" },
		{ "ASINH","ASINH" },
		{ "ATAN","ATAN" },
		{ "ATAN2","ATAN2" },
		{ "ATANH","ATANH" },
		{ "AVEDEV","AVEDEV" },
		{ "AVERAGE","AVERAGE" },
		{ "AVERAGEA","AVERAGEA" },
		{ "AVERAGEIF","AVERAGEIF" },
		/* { "ODF.AVERAGEIFS","AVERAGEIFS" },  not implemented */
		{ "BINOM.DIST.RANGE","B" },
		{ "BASE","BASE" },
		{ "BESSELI","BESSELI" },
		{ "BESSELJ","BESSELJ" },
		{ "BESSELK","BESSELK" },
		{ "BESSELY","BESSELY" },
		{ "BETADIST","BETADIST" },
		{ "BETAINV","BETAINV" },
		{ "BIN2DEC","BIN2DEC" },
		{ "BIN2HEX","BIN2HEX" },
		{ "BIN2OCT","BIN2OCT" },
		{ "BINOMDIST","BINOMDIST" },
		{ "BITAND","BITAND" },
		{ "BITLSHIFT","BITLSHIFT" },
		{ "BITOR","BITOR" },
		{ "BITRSHIFT","BITRSHIFT" },
		{ "BITXOR","BITXOR" },
		{ "CEIL", "CEILING" },
		/* { "ODF.CEILING","CEILING" },  see the handler code for CEILING*/
		{ "CELL","CELL" },
		{ "CHAR","CHAR" },
		/* { "ODF.CHISQDIST","CHISQDIST" },  we have related r.*chisq functions */
		/* { "ODF.CHISQINV","CHISQINV" },   we have related r.*chisq functions */
		{ "CHOOSE","CHOOSE" },
		{ "CLEAN","CLEAN" },
		{ "CODE","CODE" },
		{ "COLUMN","COLUMN" },
		{ "COLUMNS","COLUMNS" },
		{ "COMBIN","COMBIN" },
		{ "COMBINA","COMBINA" },
		{ "COMPLEX","COMPLEX" },
		{ "CONCATENATE","CONCATENATE" },
		{ "CONFIDENCE","CONFIDENCE" },
		{ "CONVERT","CONVERT" },
		{ "CORREL","CORREL" },
		{ "COS","COS" },
		{ "COSH","COSH" },
		{ "COT","COT" },
		{ "COTH","COTH" },
		{ "COUNT","COUNT" },
		{ "COUNTA","COUNTA" },
		{ "COUNTBLANK","COUNTBLANK" },
		{ "COUNTIF","COUNTIF" },
		/* { "COUNTIFS","COUNTIFS" },  not implemented */
		{ "COUPDAYBS","COUPDAYBS" },
		{ "COUPDAYS","COUPDAYS" },
		{ "COUPDAYSNC","COUPDAYSNC" },
		{ "COUPNCD","COUPNCD" },
		{ "COUPNUM","COUPNUM" },
		{ "COUPPCD","COUPPCD" },
		{ "COVAR","COVAR" },
		{ "CRITBINOM","CRITBINOM" },
		{ "CSC","CSC" },
		{ "CSCH","CSCH" },
		{ "CUMIPMT","CUMIPMT" },
		{ "CUMPRINC","CUMPRINC" },
		{ "DATE","DATE" },
		{ "DATEDIF","DATEDIF" },
		{ "DATEVALUE","DATEVALUE" },
		{ "DAVERAGE","DAVERAGE" },
		{ "DAY","DAY" },
		{ "DAYS","DAYS" },
		{ "DAYS360","DAYS360" },
		{ "DB","DB" },
		{ "DCOUNT","DCOUNT" },
		{ "DCOUNTA","DCOUNTA" },
		{ "DDB","DDB" },
		/* { "DDE","DDE" },  not implemented */
		{ "DEC2BIN","DEC2BIN" },
		{ "DEC2HEX","DEC2HEX" },
		{ "DEC2OCT","DEC2OCT" },
		{ "DECIMAL","DECIMAL" },
		{ "DEGREES","DEGREES" },
		{ "DELTA","DELTA" },
		{ "DEVSQ","DEVSQ" },
		{ "DGET","DGET" },
		{ "DISC","DISC" },
		{ "DMAX","DMAX" },
		{ "DMIN","DMIN" },
		{ "DOLLAR","DOLLAR" },
		{ "DOLLARDE","DOLLARDE" },
		{ "DOLLARFR","DOLLARFR" },
		{ "DPRODUCT","DPRODUCT" },
		{ "DSTDEV","DSTDEV" },
		{ "DSTDEVP","DSTDEVP" },
		{ "DSUM","DSUM" },
		{ "DURATION","DURATION" },
		{ "DVAR","DVAR" },
		{ "DVARP","DVARP" },
		{ "EDATE","EDATE" },
		{ "EFFECT","EFFECT" },
		{ "EOMONTH","EOMONTH" },
		{ "ERF","ERF" },
		{ "ERFC","ERFC" },
		{ "ERROR.TYPE","ERROR.TYPE" },
		{ "EUROCONVERT","EUROCONVERT" },
		{ "EVEN","EVEN" },
		{ "EXACT","EXACT" },
		{ "EXP","EXP" },
		{ "EXPONDIST","EXPONDIST" },
		{ "FACT","FACT" },
		{ "FACTDOUBLE","FACTDOUBLE" },
		{ "FALSE","FALSE" },
		{ "FIND","FIND" },
		{ "FINDB","FINDB" },
		{ "FISHER","FISHER" },
		{ "FISHERINV","FISHERINV" },
		{ "FIXED","FIXED" },
		{ "FLOOR","FLOOR" },
		{ "FORECAST","FORECAST" },
		{ "GET.FORMULA","FORMULA" },
		{ "FREQUENCY","FREQUENCY" },
		{ "FTEST","FTEST" },
		{ "FV","FV" },
		{ "FVSCHEDULE","FVSCHEDULE" },
		{ "GAMMA","GAMMA" },
		{ "GAMMADIST","GAMMADIST" },
		{ "GAMMAINV","GAMMAINV" },
		{ "GAMMALN","GAMMALN" },
		/* { "GAUSS","GAUSS" },  converted to ERF on import */
		{ "GCD","GCD" },
		{ "GEOMEAN","GEOMEAN" },
		{ "GESTEP","GESTEP" },
		{ "GETPIVOTDATA","GETPIVOTDATA" },
		{ "GROWTH","GROWTH" },
		{ "HARMEAN","HARMEAN" },
		{ "HEX2BIN","HEX2BIN" },
		{ "HEX2DEC","HEX2DEC" },
		{ "HEX2OCT","HEX2OCT" },
		{ "HLOOKUP","HLOOKUP" },
		{ "HOUR","HOUR" },
		{ "HYPERLINK","HYPERLINK" },
		{ "HYPGEOMDIST","HYPGEOMDIST" },
		{ "IF","IF" },
		{ "IFERROR","IFERROR" },
		{ "IFNA","IFNA" },
		{ "IMABS","IMABS" },
		{ "IMAGINARY","IMAGINARY" },
		{ "IMARGUMENT","IMARGUMENT" },
		{ "IMCONJUGATE","IMCONJUGATE" },
		{ "IMCOS","IMCOS" },
		{ "IMCOT","IMCOT" },
		{ "IMCSC","IMCSC" },
		{ "IMCSCH","IMCSCH" },
		{ "IMDIV","IMDIV" },
		{ "IMEXP","IMEXP" },
		{ "IMLN","IMLN" },
		{ "IMLOG10","IMLOG10" },
		{ "IMLOG2","IMLOG2" },
		{ "IMPOWER","IMPOWER" },
		{ "IMPRODUCT","IMPRODUCT" },
		{ "IMREAL","IMREAL" },
		{ "IMSEC","IMSEC" },
		{ "IMSECH","IMSECH" },
		{ "IMSIN","IMSIN" },
		{ "IMSQRT","IMSQRT" },
		{ "IMSUB","IMSUB" },
		{ "IMSUM","IMSUM" },
		{ "IMTAN","IMTAN" },
		{ "INDEX","INDEX" },
		{ "INDIRECT","INDIRECT" },
		{ "INFO","INFO" },
		{ "INT","INT" },
		{ "INTERCEPT","INTERCEPT" },
		{ "INTRATE","INTRATE" },
		{ "IPMT","IPMT" },
		{ "IRR","IRR" },
		{ "ISBLANK","ISBLANK" },
		{ "ISERR","ISERR" },
		{ "ISERROR","ISERROR" },
		{ "ISEVEN","ISEVEN" },
		{ "ISFORMULA","ISFORMULA" },
		{ "ISLOGICAL","ISLOGICAL" },
		{ "ISNA","ISNA" },
		{ "ISNONTEXT","ISNONTEXT" },
		{ "ISNUMBER","ISNUMBER" },
		{ "ISODD","ISODD" },
		{ "ISOWEEKNUM","ISOWEEKNUM" },
		{ "ISPMT","ISPMT" },
		{ "ISREF","ISREF" },
		{ "ISTEXT","ISTEXT" },
		{ "JIS","JIS" },
		{ "KURT","KURT" },
		{ "LARGE","LARGE" },
		{ "LCM","LCM" },
		{ "LEFT","LEFT" },
		{ "LEFTB","LEFTB" },
		{ "CHIDIST","LEGACY.CHIDIST" },
		{ "CHIINV","LEGACY.CHIINV" },
		{ "CHITEST","LEGACY.CHITEST" },
		{ "FDIST","LEGACY.FDIST" },
		{ "FINV","LEGACY.FINV" },
		{ "NORMSDIST","LEGACY.NORMSDIST" },
		{ "NORMSINV","LEGACY.NORMSINV" },
		{ "LEN","LEN" },
		{ "LENB","LENB" },
		{ "LINEST","LINEST" },
		{ "LN","LN" },
		{ "LOG","LOG" },
		{ "LOG10","LOG10" },
		{ "LOGEST","LOGEST" },
		{ "LOGINV","LOGINV" },
		{ "LOGNORMDIST","LOGNORMDIST" },
		{ "LOOKUP","LOOKUP" },
		{ "LOWER","LOWER" },
		{ "MATCH","MATCH" },
		{ "MAX","MAX" },
		{ "MAXA","MAXA" },
		{ "MDETERM","MDETERM" },
		{ "MDURATION","MDURATION" },
		{ "MEDIAN","MEDIAN" },
		{ "MID","MID" },
		{ "MIDB","MIDB" },
		{ "MIN","MIN" },
		{ "MINA","MINA" },
		{ "MINUTE","MINUTE" },
		{ "MINVERSE","MINVERSE" },
		{ "MIRR","MIRR" },
		{ "MMULT","MMULT" },
		{ "MOD","MOD" },
		{ "MODE","MODE" },
		{ "MONTH","MONTH" },
		{ "MROUND","MROUND" },
		{ "MULTINOMIAL","MULTINOMIAL" },
		/* { "MULTIPLE.OPERATIONS","MULTIPLE.OPERATIONS" },  not implemented */
		{ "MUNIT","MUNIT" },
		{ "N","N" },
		{ "NA","NA" },
		{ "NEGBINOMDIST","NEGBINOMDIST" },
		{ "NETWORKDAYS","NETWORKDAYS" },
		{ "NOMINAL","NOMINAL" },
		{ "NORMDIST","NORMDIST" },
		{ "NORMINV","NORMINV" },
		{ "NOT","NOT" },
		{ "NOW","NOW" },
		{ "NPER","NPER" },
		{ "NPV","NPV" },
		{ "NUMBERVALUE","NUMBERVALUE" },
		{ "OCT2BIN","OCT2BIN" },
		{ "OCT2DEC","OCT2DEC" },
		{ "OCT2HEX","OCT2HEX" },
		{ "ODD","ODD" },
		{ "ODDFPRICE","ODDFPRICE" },
		{ "ODDFYIELD","ODDFYIELD" },
		{ "ODDLPRICE","ODDLPRICE" },
		{ "ODDLYIELD","ODDLYIELD" },
		{ "OFFSET","OFFSET" },
		{ "OR","OR" },
		{ "G_DURATION","PDURATION" },
		{ "PEARSON","PEARSON" },
		{ "PERCENTILE","PERCENTILE" },
		{ "PERCENTRANK","PERCENTRANK" },
		{ "PERMUT","PERMUT" },
		{ "PERMUTATIONA","PERMUTATIONA" },
		/* { "PHI","PHI" },  converted to NORMDIST on import */
		{ "PI","PI" },
		{ "PMT","PMT" },
		{ "POISSON","POISSON" },
		{ "POWER","POWER" },
		{ "PPMT","PPMT" },
		{ "PRICE","PRICE" },
		{ "PRICEDISC","PRICEDISC" },
		{ "PRICEMAT","PRICEMAT" },
		{ "PROB","PROB" },
		{ "PRODUCT","PRODUCT" },
		{ "PROPER","PROPER" },
		{ "PV","PV" },
		{ "QUARTILE","QUARTILE" },
		{ "QUOTIENT","QUOTIENT" },
		{ "RADIANS","RADIANS" },
		{ "RAND","RAND" },
		{ "RANDBETWEEN","RANDBETWEEN" },
		{ "RANK","RANK" },
		{ "RATE","RATE" },
		{ "RECEIVED","RECEIVED" },
		{ "REPLACE","REPLACE" },
		{ "REPLACEB","REPLACEB" },
		{ "REPT","REPT" },
		{ "RIGHT","RIGHT" },
		{ "RIGHTB","RIGHTB" },
		{ "ROMAN","ROMAN" },
		{ "ROUND","ROUND" },
		{ "ROUNDDOWN","ROUNDDOWN" },
		{ "ROUNDUP","ROUNDUP" },
		{ "ROW","ROW" },
		{ "ROWS","ROWS" },
		{ "RRI","RRI" },
		{ "RSQ","RSQ" },
		{ "SEARCH","SEARCH" },
		{ "SEARCHB","SEARCHB" },
		{ "SEC","SEC" },
		{ "SECH","SECH" },
		{ "SECOND","SECOND" },
		{ "SERIESSUM","SERIESSUM" },
		{ "SHEET","SHEET" },
		{ "SHEETS","SHEETS" },
		{ "SIGN","SIGN" },
		{ "SIN","SIN" },
		{ "SINH","SINH" },
		{ "SKEW","SKEW" },
		{ "SKEWP","SKEWP" },
		{ "SLN","SLN" },
		{ "SLOPE","SLOPE" },
		{ "SMALL","SMALL" },
		{ "SQRT","SQRT" },
		{ "SQRTPI","SQRTPI" },
		{ "STANDARDIZE","STANDARDIZE" },
		{ "STDEV","STDEV" },
		{ "STDEVA","STDEVA" },
		{ "STDEVP","STDEVP" },
		{ "STDEVPA","STDEVPA" },
		{ "STEYX","STEYX" },
		{ "SUBSTITUTE","SUBSTITUTE" },
		{ "SUBTOTAL","SUBTOTAL" },
		{ "SUM","SUM" },
		{ "SUMIF","SUMIF" },
		/* { "SUMIFS","SUMIFS" },  not implemented */
		{ "SUMPRODUCT","SUMPRODUCT" },
		{ "SUMSQ","SUMSQ" },
		{ "SUMX2MY2","SUMX2MY2" },
		{ "SUMX2PY2","SUMX2PY2" },
		{ "SUMXMY2","SUMXMY2" },
		{ "SYD","SYD" },
		{ "T","T" },
		{ "TAN","TAN" },
		{ "TANH","TANH" },
		{ "TBILLEQ","TBILLEQ" },
		{ "TBILLPRICE","TBILLPRICE" },
		{ "TBILLYIELD","TBILLYIELD" },
		{ "TDIST","TDIST" },
		{ "TEXT","TEXT" },
		{ "TIME","TIME" },
		{ "TIMEVALUE","TIMEVALUE" },
		{ "TINV","TINV" },
		{ "TODAY","TODAY" },
		{ "TRANSPOSE","TRANSPOSE" },
		{ "TREND","TREND" },
		{ "TRIM","TRIM" },
		{ "TRIMMEAN","TRIMMEAN" },
		{ "TRUE","TRUE" },
		{ "TRUNC","TRUNC" },
		{ "TTEST","TTEST" },
		{ "TYPE","TYPE" },
		{ "UNICHAR","UNICHAR" },
		{ "UNICODE","UNICODE" },
		/* { "USDOLLAR","USDOLLAR" }, this is a synonym to DOLLAR */
		{ "UPPER","UPPER" },
		{ "VALUE","VALUE" },
		{ "VAR","VAR" },
		{ "VARA","VARA" },
		{ "VARP","VARP" },
		{ "VARPA","VARPA" },
		{ "VDB","VDB" },
		{ "VLOOKUP","VLOOKUP" },
		{ "WEEKDAY","WEEKDAY" },
		{ "WEEKNUM","WEEKNUM" },
		{ "WEIBULL","WEIBULL" },
		{ "WORKDAY","WORKDAY" },
		{ "XIRR","XIRR" },
		{ "XNPV","XNPV" },
		{ "XOR","XOR" },
		{ "YEAR","YEAR" },
		{ "YEARFRAC","YEARFRAC" },
		{ "YIELD","YIELD" },
		{ "YIELDDISC","YIELDDISC" },
		{ "YIELDMAT","YIELDMAT" },
		{ "ZTEST","ZTEST" },
		{ NULL, NULL }
	};
	static GHashTable *namemap = NULL;
	static GHashTable *handlermap = NULL;

	char const *name = gnm_func_get_name (func->func);
	gboolean (*handler) (GnmConventionsOut *out, GnmExprFunction const *func);

	if (NULL == namemap) {
		guint i;
		namemap = g_hash_table_new (go_ascii_strcase_hash,
					    go_ascii_strcase_equal);
		for (i = 0; sc_func_renames[i].gnm_name; i++)
			g_hash_table_insert (namemap,
					     (gchar *) sc_func_renames[i].gnm_name,
					     (gchar *) sc_func_renames[i].odf_name);
	}
	if (NULL == handlermap) {
		guint i;
		handlermap = g_hash_table_new (go_ascii_strcase_hash,
					       go_ascii_strcase_equal);
		for (i = 0; sc_func_handlers[i].gnm_name; i++)
			g_hash_table_insert (handlermap,
					     (gchar *) sc_func_handlers[i].gnm_name,
					     sc_func_handlers[i].handler);
	}

	handler = g_hash_table_lookup (handlermap, name);

	if (handler == NULL || !handler (out, func)) {
		char const *new_name = g_hash_table_lookup (namemap, name);
		GString *target = out->accum;

		if (new_name == NULL) {
			if (0 == g_ascii_strncasecmp (name, "ODF.", 4)) {
				char *new_u_name;
				new_u_name = g_ascii_strup (name + 4, -1);
				g_string_append (target,  new_u_name);
				g_free (new_u_name);
			} else {
				char *new_u_name;
				g_string_append (target, "ORG.GNUMERIC.");
				new_u_name = g_ascii_strup (name, -1);
				g_string_append (target, new_u_name);
				g_free (new_u_name);
			}
		}
		else
			g_string_append (target, new_name);

		gnm_expr_list_as_string (func->argc, func->argv, out);
	}
	return;
}



static GnmConventions *
odf_expr_conventions_new (void)
{
	GnmConventions *conv;

	conv = gnm_conventions_new ();
	conv->sheet_name_sep		= '.';
	conv->arg_sep			= ';';
	conv->array_col_sep		= ';';
	conv->array_row_sep		= '|';
	conv->intersection_char         = '!';
	conv->decimal_sep_dot		= TRUE;
	conv->output.cell_ref		= odf_cellref_as_string;
	conv->output.range_ref		= odf_rangeref_as_string;
	conv->output.func               = odf_expr_func_handler;

	return conv;
}

static gboolean
odf_cell_is_covered (Sheet const *sheet, GnmCell *current_cell,
		    int col, int row, GnmRange const *merge_range,
		    GSList **merge_ranges)
{
	GSList *l;

	if (merge_range != NULL) {
		GnmRange *new_range = g_new(GnmRange, 1);
		*new_range = *merge_range;
		(*merge_ranges) = g_slist_prepend (*merge_ranges, new_range);
		return FALSE;
	}

	if ((*merge_ranges) == NULL)
		return FALSE;

	*merge_ranges = g_slist_remove_all (*merge_ranges, NULL);

	for (l = *merge_ranges; l != NULL; l = g_slist_next(l)) {
		GnmRange *r = l->data;
		if (r->end.row < row) {
			/* We do not need this range anymore */
			g_free (r);
			l->data = NULL;
			continue;
		}
		/* no need to check for beginning rows */
		/* we have to check for column range */
		if ((r->start.col <= col) && (col <= r->end.col))
			return TRUE;
	}
	return FALSE;
}

static void
odf_write_comment (GnmOOExport *state, GnmComment const *cc)
{
	char const *author;
	char const *text;
	const PangoAttrList * markup;
	gboolean pp = TRUE;

	g_object_get (G_OBJECT (state->xml), "pretty-print", &pp, NULL);

	g_object_get (G_OBJECT (cc), "text", &text,
		      "markup", &markup, "author", &author,  NULL);

	gsf_xml_out_start_element (state->xml, OFFICE "annotation");
	if (author != NULL) {
		gsf_xml_out_start_element (state->xml, DUBLINCORE "creator");
		gsf_xml_out_add_cstr (state->xml, NULL, author);
		gsf_xml_out_end_element (state->xml); /*  DUBLINCORE "creator" */;
	}
	g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
	gsf_xml_out_start_element (state->xml, TEXT "p");
	if (markup != NULL)
		odf_new_markup (state, markup, text);
	else {
		gboolean white_written = TRUE;
		odf_add_chars (state, text, strlen (text), &white_written);
	}
	gsf_xml_out_end_element (state->xml);   /* p */
	g_object_set (G_OBJECT (state->xml), "pretty-print", pp, NULL);
	gsf_xml_out_end_element (state->xml); /*  OFFICE "annotation" */
}

static char *
odf_strip_brackets (char *string)
{
	char *closing;
	closing = strrchr(string, ']');
	if (closing != NULL)
		*closing = '\0';
	return ((*string == '[') ? (string + 1) : string);
}

static char *
odf_graph_get_series (GnmOOExport *state, GogGraph *sog, GnmParsePos *pp)
{
	GSList *list = gog_graph_get_data (sog);
	GString *str = g_string_new (NULL);

	for (;list != NULL; list = list->next) {
		GOData *dat = list->data;
		GnmExprTop const *texpr = gnm_go_data_get_expr (dat);
		if (texpr != NULL && gnm_expr_top_is_rangeref (texpr)) {
			char *formula = gnm_expr_top_as_string (texpr, pp, state->conv);
			g_string_append (str, odf_strip_brackets (formula));
			g_string_append_c (str, ' ');
			g_free (formula);
		}
	}

	return g_string_free (str, FALSE);
}

static void
odf_write_frame_size (GnmOOExport *state, SheetObject *so)
{
	SheetObjectAnchor const *anchor = sheet_object_get_anchor (so);
	double res_pts[4] = {0.,0.,0.,0.};
	GnmRange const *r = &anchor->cell_bound;
	GnmCellRef ref;
	GnmExprTop const *texpr;
	GnmParsePos pp;
	char *formula;

	sheet_object_anchor_to_offset_pts (anchor, state->sheet, res_pts);

	odf_add_pt (state->xml, SVG "x", res_pts[0]);
	odf_add_pt (state->xml, SVG "y", res_pts[1]);
	odf_add_pt (state->xml, TABLE "end-x", res_pts[2]);
	odf_add_pt (state->xml, TABLE "end-y", res_pts[3]);

	/* The next 3 lines should not be needed, but older versions of Gnumeric used the */
	/* width and height. */
	sheet_object_anchor_to_pts (anchor, state->sheet, res_pts);
	odf_add_pt (state->xml, SVG "width", res_pts[2] - res_pts[0]);
	odf_add_pt (state->xml, SVG "height", res_pts[3] - res_pts[1]);

	gnm_cellref_init (&ref, (Sheet *) state->sheet, r->end.col, r->end.row, TRUE);
	texpr =  gnm_expr_top_new (gnm_expr_new_cellref (&ref));
	parse_pos_init_sheet (&pp, state->sheet);
	formula = gnm_expr_top_as_string (texpr, &pp, state->conv);
	gnm_expr_top_unref (texpr);
	gsf_xml_out_add_cstr (state->xml, TABLE "end-cell-address",
			      odf_strip_brackets (formula));
	g_free (formula);
}

static void
odf_write_graph (GnmOOExport *state, SheetObject *so, char const *name)
{
	GnmParsePos pp;
	parse_pos_init_sheet (&pp, state->sheet);

	if (name != NULL) {
		char *full_name = g_strdup_printf ("%s/", name);
		gsf_xml_out_start_element (state->xml, DRAW "object");
		gsf_xml_out_add_cstr (state->xml, XLINK "href", full_name);
		g_free (full_name);
		gsf_xml_out_add_cstr (state->xml, XLINK "type", "simple");
		gsf_xml_out_add_cstr (state->xml, XLINK "show", "embed");
		gsf_xml_out_add_cstr (state->xml, XLINK "actuate", "onLoad");
		full_name = odf_graph_get_series (state, sheet_object_graph_get_gog (so), &pp);
		gsf_xml_out_add_cstr (state->xml, DRAW "notify-on-update-of-ranges",
				      full_name);
		g_free (full_name);
		gsf_xml_out_end_element (state->xml); /*  DRAW "object" */
		full_name = g_strdup_printf ("Pictures/%s", name);
		gsf_xml_out_start_element (state->xml, DRAW "image");
		gsf_xml_out_add_cstr (state->xml, XLINK "href", full_name);
		g_free (full_name);
		gsf_xml_out_add_cstr (state->xml, XLINK "type", "simple");
		gsf_xml_out_add_cstr (state->xml, XLINK "show", "embed");
		gsf_xml_out_add_cstr (state->xml, XLINK "actuate", "onLoad");
		gsf_xml_out_end_element (state->xml); /*  DRAW "image" */
		full_name = g_strdup_printf ("Pictures/%s.png", name);
		gsf_xml_out_start_element (state->xml, DRAW "image");
		gsf_xml_out_add_cstr (state->xml, XLINK "href", full_name);
		g_free (full_name);
		gsf_xml_out_add_cstr (state->xml, XLINK "type", "simple");
		gsf_xml_out_add_cstr (state->xml, XLINK "show", "embed");
		gsf_xml_out_add_cstr (state->xml, XLINK "actuate", "onLoad");
		gsf_xml_out_end_element (state->xml); /*  DRAW "image" */
	} else
		g_warning ("Graph is missing from hash.");
}

static void
odf_write_image (GnmOOExport *state, SheetObject *so, char const *name)
{
	if (name != NULL) {
		char *image_type;
		char *fullname;
		g_object_get (G_OBJECT (so),
			      "image-type", &image_type,
			      NULL);
		fullname = g_strdup_printf ("Pictures/%s.%s", name, image_type);

		gsf_xml_out_start_element (state->xml, DRAW "image");
		gsf_xml_out_add_cstr (state->xml, XLINK "href", fullname);
		gsf_xml_out_add_cstr (state->xml, XLINK "type", "simple");
		gsf_xml_out_add_cstr (state->xml, XLINK "show", "embed");
		gsf_xml_out_add_cstr (state->xml, XLINK "actuate", "onLoad");
		gsf_xml_out_end_element (state->xml); /*  DRAW "image" */

		g_free(fullname);
		g_free (image_type);
	} else
		g_warning ("Image is missing from hash.");
}

static void
odf_write_frame (GnmOOExport *state, SheetObject *so)
{
	gsf_xml_out_start_element (state->xml, DRAW "frame");

	odf_write_frame_size (state, so);

	if (IS_SHEET_OBJECT_GRAPH (so))
		odf_write_graph (state, so, g_hash_table_lookup (state->graphs, so));
	else if (IS_SHEET_OBJECT_IMAGE (so))
		odf_write_image (state, so, g_hash_table_lookup (state->images, so));
	else {
		gsf_xml_out_start_element (state->xml, DRAW "text-box");
		gsf_xml_out_simple_element (state->xml, TEXT "p",
					    "Missing Framed Sheet Object");
		gsf_xml_out_end_element (state->xml); /*  DRAW "text-box" */
	}

	gsf_xml_out_end_element (state->xml); /*  DRAW "frame" */
}

static void
odf_write_control (GnmOOExport *state, SheetObject *so, char const *id)
{
	gsf_xml_out_start_element (state->xml, DRAW "control");
	odf_write_frame_size (state, so);
	gsf_xml_out_add_cstr (state->xml, DRAW "control", id);
	gsf_xml_out_end_element (state->xml); /*  DRAW "control" */
}

static void
odf_write_so_filled (GnmOOExport *state, SheetObject *so)
{
	char const *element;
	gboolean is_oval = FALSE;
	gchar *text = NULL;
	gchar const *style_name = g_hash_table_lookup (state->so_styles, so);

	g_object_get (G_OBJECT (so), "is-oval", &is_oval, "text", &text, NULL);
	element = is_oval ? DRAW "ellipse" : DRAW "rect";

	gsf_xml_out_start_element (state->xml, element);
	if (style_name != NULL)
		gsf_xml_out_add_cstr (state->xml, DRAW "style-name", style_name);
	odf_write_frame_size (state, so);
	gsf_xml_out_simple_element (state->xml, TEXT "p", text);
	g_free (text);
	gsf_xml_out_end_element (state->xml); /*  DRAW "rect" or "ellipse" */
}

static void
odf_write_line (GnmOOExport *state, SheetObject *so)
{
	SheetObjectAnchor const *anchor = sheet_object_get_anchor (so);
	double res_pts[4] = {0.,0.,0.,0.};
	GnmRange const *r = &anchor->cell_bound;
	GnmCellRef ref;
	GnmExprTop const *texpr;
	GnmParsePos pp;
	char *formula;
	double x1, y1, x2, y2;
	gchar const *style_name = g_hash_table_lookup (state->so_styles, so);

	gsf_xml_out_start_element (state->xml, DRAW "line");
	if (style_name != NULL)
		gsf_xml_out_add_cstr (state->xml, DRAW "style-name", style_name);

	sheet_object_anchor_to_offset_pts (anchor, state->sheet, res_pts);
	odf_add_pt (state->xml, TABLE "end-x", res_pts[2]);
	odf_add_pt (state->xml, TABLE "end-y", res_pts[3]);
	sheet_object_anchor_to_pts (anchor, state->sheet, res_pts);

	switch (anchor->base.direction) {
	default:
	case GOD_ANCHOR_DIR_UNKNOWN:
	case GOD_ANCHOR_DIR_UP_RIGHT:
		x1 = res_pts[0];
		x2 = res_pts[2];
		y1 = res_pts[3];
		y2 = res_pts[1];
		break;
	case GOD_ANCHOR_DIR_DOWN_RIGHT:
		x1 = res_pts[0];
		x2 = res_pts[2];
		y1 = res_pts[1];
		y2 = res_pts[3];
		break;
	case GOD_ANCHOR_DIR_UP_LEFT:
		x1 = res_pts[2];
		x2 = res_pts[0];
		y1 = res_pts[3];
		y2 = res_pts[1];
		break;
	case GOD_ANCHOR_DIR_DOWN_LEFT:
		x1 = res_pts[2];
		x2 = res_pts[0];
		y1 = res_pts[1];
		y2 = res_pts[3];
		break;
	}

	odf_add_pt (state->xml, SVG "x1", x1);
	odf_add_pt (state->xml, SVG "y1", y1);
	odf_add_pt (state->xml, SVG "x2", x2);
	odf_add_pt (state->xml, SVG "y2", y2);

	gnm_cellref_init (&ref, (Sheet *) state->sheet, r->end.col, r->end.row, TRUE);
	texpr =  gnm_expr_top_new (gnm_expr_new_cellref (&ref));
	parse_pos_init_sheet (&pp, state->sheet);
	formula = gnm_expr_top_as_string (texpr, &pp, state->conv);
	gnm_expr_top_unref (texpr);
	gsf_xml_out_add_cstr (state->xml, TABLE "end-cell-address",
			      odf_strip_brackets (formula));
	g_free (formula);

	gsf_xml_out_end_element (state->xml); /*  DRAW "line" */
}

static void
odf_write_objects (GnmOOExport *state, GSList *objects)
{
	GSList *l;

	for (l = objects; l != NULL; l = l->next) {
		SheetObject *so = l->data;
		char const *id = g_hash_table_lookup (state->controls, so);
		if (so == NULL) {
			g_warning ("NULL sheet object encountered.");
			continue;
		}
		if (IS_GNM_FILTER_COMBO (so) || IS_GNM_VALIDATION_COMBO(so))
			continue;
		if (id != NULL)
			odf_write_control (state, so, id);
		else if (IS_CELL_COMMENT (so))
			odf_write_comment (state, CELL_COMMENT (so));
		else if (IS_GNM_SO_FILLED (so))
			odf_write_so_filled (state, so);
		else if (IS_GNM_SO_LINE (so))
			odf_write_line (state, so);
		else
			odf_write_frame (state, so);

	}
}

static void
odf_write_link_start (GnmOOExport *state, GnmHLink *link)
{
	if (link == NULL)
		return;
	gsf_xml_out_start_element (state->xml, TEXT "a");
	gsf_xml_out_add_cstr (state->xml, XLINK "type", "simple");
	gsf_xml_out_add_cstr (state->xml, XLINK "actuate", "onRequest");
	gsf_xml_out_add_cstr (state->xml, XLINK "href", gnm_hlink_get_target (link));
	gsf_xml_out_add_cstr (state->xml, OFFICE "title", gnm_hlink_get_tip (link));
}

static void
odf_write_link_end (GnmOOExport *state, GnmHLink *link)
{
	if (link != NULL)
		gsf_xml_out_end_element (state->xml);  /* a */
}


static void
odf_write_empty_cell (GnmOOExport *state, int num, GnmStyle const *style, GSList *objects)
{
	if (num > 0) {
		gsf_xml_out_start_element (state->xml, TABLE "table-cell");
		if (num > 1)
			gsf_xml_out_add_int (state->xml,
					     TABLE "number-columns-repeated",
					     num);
		if (style != NULL) {
			char const * name = odf_find_style (state, style);
			GnmValidation const *val = gnm_style_get_validation (style);
			if (name != NULL)
				gsf_xml_out_add_cstr (state->xml,
						      TABLE "style-name", name);
			if (val != NULL) {
				char *vname = g_strdup_printf ("VAL-%p", val);
				gsf_xml_out_add_cstr (state->xml,
						      TABLE "content-validation-name", vname);
				g_free (vname);
			}
				
		}
		odf_write_objects (state, objects);
		gsf_xml_out_end_element (state->xml);   /* table-cell */
	}
}

static void
odf_write_covered_cell (GnmOOExport *state, int *num)
{
	if (*num > 0) {
		gsf_xml_out_start_element (state->xml, TABLE "covered-table-cell");
		if (*num > 1)
			gsf_xml_out_add_int (state->xml,
					     TABLE "number-columns-repeated",
					     *num);
		gsf_xml_out_end_element (state->xml);   /* covered-table-cell */
		*num = 0;
	}
}

static void
odf_write_cell (GnmOOExport *state, GnmCell *cell, GnmRange const *merge_range,
		GSList *objects)
{
	int rows_spanned = 0, cols_spanned = 0;
	gboolean pp = TRUE;
	GnmHLink *link = NULL;

	g_object_get (G_OBJECT (state->xml), "pretty-print", &pp, NULL);

	if (merge_range != NULL) {
		rows_spanned = merge_range->end.row - merge_range->start.row + 1;
		cols_spanned = merge_range->end.col - merge_range->start.col + 1;
	}

	gsf_xml_out_start_element (state->xml, TABLE "table-cell");

	if (cols_spanned > 1)
		gsf_xml_out_add_int (state->xml,
				     TABLE "number-columns-spanned", cols_spanned);
	if (rows_spanned > 1)
		gsf_xml_out_add_int (state->xml,
				     TABLE "number-rows-spanned", rows_spanned);
	if (cell != NULL) {
		GnmStyle const *style = gnm_cell_get_style (cell);

		if (style) {
			char const * name = odf_find_style (state, style);
			GnmValidation const *val = gnm_style_get_validation (style);
			if (name != NULL)
				gsf_xml_out_add_cstr (state->xml,
						      TABLE "style-name", name);
			if (val != NULL) {
				char *vname = g_strdup_printf ("VAL-%p", val);
				gsf_xml_out_add_cstr (state->xml,
						      TABLE "content-validation-name", vname);
				g_free (vname);
			}
			link = gnm_style_get_hlink (style);
		}

		if ((NULL != cell->base.texpr) &&
		    !gnm_expr_top_is_array_elem (cell->base.texpr, NULL, NULL)) {
			char *formula, *eq_formula;
			GnmParsePos pp;

			if (gnm_cell_is_array (cell)) {
				GnmExprArrayCorner const *ac;

				ac = gnm_expr_top_get_array_corner (cell->base.texpr);
				if (ac != NULL) {
					gsf_xml_out_add_uint (state->xml,
							      TABLE "number-matrix-columns-spanned",
							      (unsigned int)(ac->cols));
					gsf_xml_out_add_uint (state->xml,
							      TABLE "number-matrix-rows-spanned",
							      (unsigned int)(ac->rows));
				}
			}

			parse_pos_init_cell (&pp, cell);
			formula = gnm_expr_top_as_string (cell->base.texpr,
							  &pp,
							  state->conv);
			eq_formula = g_strdup_printf ("of:=%s", formula);

			gsf_xml_out_add_cstr (state->xml,
					      TABLE "formula",
					      eq_formula);
			g_free (formula);
			g_free (eq_formula);
		}

		switch (cell->value->type) {
		case VALUE_EMPTY:
			break;
		case VALUE_BOOLEAN:
			gsf_xml_out_add_cstr_unchecked (state->xml,
							OFFICE "value-type", "boolean");
			odf_add_bool (state->xml, OFFICE "boolean-value",
				value_get_as_bool (cell->value, NULL));
			break;
		case VALUE_FLOAT: {
			GOFormat const *fmt = gnm_cell_get_format (cell);
			if (go_format_is_date (fmt)) {
				char *str;
				gnm_float f = value_get_as_float (cell->value);
				if (f == gnm_floor (f)) {
					gsf_xml_out_add_cstr_unchecked (state->xml,
									OFFICE "value-type", "date");
					str = format_value (state->date_fmt, cell->value, NULL, -1, workbook_date_conv (state->wb));
					gsf_xml_out_add_cstr (state->xml, OFFICE "date-value", str);
				} else {
					gsf_xml_out_add_cstr_unchecked (state->xml,
									OFFICE "value-type", "date");
					str = format_value (state->date_long_fmt, cell->value, NULL, -1, workbook_date_conv (state->wb));
					gsf_xml_out_add_cstr (state->xml, OFFICE "date-value", str);
				}
				g_free (str);
			} else if (go_format_is_time (fmt)) {
				char *str;
				gsf_xml_out_add_cstr_unchecked (state->xml,
								OFFICE "value-type", "time");
				str = format_value (state->time_fmt, cell->value, NULL, -1, workbook_date_conv (state->wb));
				gsf_xml_out_add_cstr (state->xml, OFFICE "time-value", str);
				g_free (str);
			} else {
				GString *str = g_string_new (NULL);

				gsf_xml_out_add_cstr_unchecked (state->xml,
								OFFICE "value-type", "float");
				value_get_as_gstring (cell->value, str, state->conv);
				gsf_xml_out_add_cstr (state->xml, OFFICE "value", str->str);

				g_string_free (str, TRUE);
			}
			break;
		}
		case VALUE_ERROR:
			if (NULL == cell->base.texpr) {
				/* see https://bugzilla.gnome.org/show_bug.cgi?id=610175 */
				/* this is the same that Excel does, OOo does not have   */
				/* error literals. ODF 1.2 might be introducing a new    */
				/* value-type to address this issue                      */
				char *eq_formula = g_strdup_printf
					("of:=%s", value_peek_string (cell->value));
				gsf_xml_out_add_cstr (state->xml,
						      TABLE "formula",
						      eq_formula);
				g_free (eq_formula);
			}
			gsf_xml_out_add_cstr_unchecked (state->xml,
							OFFICE "value-type", "string");
			gsf_xml_out_add_cstr (state->xml,
					      OFFICE "string-value",
					      value_peek_string (cell->value));
			break;
		case VALUE_STRING:
			gsf_xml_out_add_cstr_unchecked (state->xml,
							OFFICE "value-type", "string");
			gsf_xml_out_add_cstr (state->xml,
					      OFFICE "string-value",
					      value_peek_string (cell->value));
			break;
		case VALUE_CELLRANGE:
		case VALUE_ARRAY:
		default:
			break;
		}
	}

	odf_write_objects (state, objects);

	if (cell != NULL && cell->value != NULL) {
		g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
		if ((VALUE_FMT (cell->value) == NULL)
		    || (!VALUE_IS_STRING (cell->value))
		    || (!go_format_is_markup (VALUE_FMT (cell->value)))) {
			char *rendered_string = gnm_cell_get_rendered_text (cell);
			gboolean white_written = TRUE;

			if (*rendered_string != '\0' || link != NULL) {
				gsf_xml_out_start_element (state->xml, TEXT "p");
				odf_write_link_start (state, link);
				if (*rendered_string != '\0')
					odf_add_chars (state, rendered_string, strlen (rendered_string),
						       &white_written);
				odf_write_link_end (state, link);
				gsf_xml_out_end_element (state->xml);   /* p */
			}
			g_free (rendered_string);
		} else {
			GString *str = g_string_new (NULL);
			const PangoAttrList * markup;

			value_get_as_gstring (cell->value, str, state->conv);
			markup = go_format_get_markup (VALUE_FMT (cell->value));

			gsf_xml_out_start_element (state->xml, TEXT "p");
			odf_write_link_start (state, link);
			odf_new_markup (state, markup, str->str);
			odf_write_link_end (state, link);
			gsf_xml_out_end_element (state->xml);   /* p */

			g_string_free (str, TRUE);
		}
		g_object_set (G_OBJECT (state->xml), "pretty-print", pp, NULL);
	}


	gsf_xml_out_end_element (state->xml);   /* table-cell */
}

static GnmStyle *
filter_style (GnmStyle *default_style, GnmStyle * this)
{
	return ((default_style == this) ? NULL : this);
}

static void
write_col_style (GnmOOExport *state, GnmStyle *col_style, ColRowInfo const *ci,
		 Sheet const *sheet)
{
	char const * name;

	if (col_style != NULL) {
		name = odf_find_style (state, col_style);
		if (name != NULL)
			gsf_xml_out_add_cstr (state->xml,
					      TABLE "default-cell-style-name", name);
	}
	name = odf_find_col_style (state,
				   (ci == NULL) ? &sheet->cols.default_style: ci,
				   FALSE);
	if (name != NULL)
		gsf_xml_out_add_cstr (state->xml, TABLE "style-name", name);
}

static void
odf_write_formatted_columns (GnmOOExport *state, Sheet const *sheet, GnmStyle **col_styles, int from, int to)
{
	int number_cols_rep;
	ColRowInfo const *last_ci;
	GnmStyle *last_col_style = NULL;
	int i;

	gsf_xml_out_start_element (state->xml, TABLE "table-column");
	number_cols_rep = 1;
	last_col_style = filter_style (state->default_style, col_styles[0]);
	last_ci = sheet_col_get (sheet, 0);
	write_col_style (state, last_col_style, last_ci, sheet);

	for (i = from+1; i < to; i++) {
		GnmStyle *this_col_style = filter_style (state->default_style, col_styles[i]);
		ColRowInfo const *this_ci = sheet_col_get (sheet, i);

		if ((this_col_style == last_col_style) && colrow_equal (last_ci, this_ci))
			number_cols_rep++;
		else {
			if (number_cols_rep > 1)
				gsf_xml_out_add_int (state->xml, TABLE "number-columns-repeated",
						     number_cols_rep);
			gsf_xml_out_end_element (state->xml); /* table-column */

			gsf_xml_out_start_element (state->xml, TABLE "table-column");
			number_cols_rep = 1;
			last_col_style = this_col_style;
			last_ci = this_ci;
			write_col_style (state, last_col_style, last_ci, sheet);
		}
	}

	if (number_cols_rep > 1)
		gsf_xml_out_add_int (state->xml, TABLE "number-columns-repeated",
				     number_cols_rep);
	gsf_xml_out_end_element (state->xml); /* table-column */
}

static void
write_row_style (GnmOOExport *state, ColRowInfo const *ci,
		 Sheet const *sheet)
{
	char const * name;

	name = odf_find_row_style (state,
				   (ci == NULL) ? &sheet->rows.default_style: ci,
				   FALSE);
	if (name != NULL)
		gsf_xml_out_add_cstr (state->xml, TABLE "style-name", name);
}


static gint
finder (gconstpointer a, gconstpointer b)
{
	GnmStyleRegion const *region = a;
	GnmCellPos const *where = b;

	return !range_contains ((&region->range), where->col, where->row);
}

static int
write_styled_cells (GnmOOExport *state, Sheet const *sheet, int row, int row_length,
		    int max_rows, GnmStyleList *list)
{
	int answer = max_rows;
	GnmCellPos where;
	where.row = row;

	for (where.col = 0; where.col < row_length; ) {
		GSList* l = g_slist_find_custom (list, &where, finder);

		if (l == NULL) {
			answer = 1;
			odf_write_empty_cell (state, 1, NULL, NULL);
			where.col++;
		} else {
			GnmStyleRegion *region = l->data;
			int repetition = region->range.end.col - where.col + 1;
			int rows = region->range.end.row - where.row + 1;

			odf_write_empty_cell (state, repetition, region->style, NULL);
			where.col += repetition;
			if (rows < answer)
				answer = rows;
		}
	}
	return answer;
}

static void
odf_write_styled_empty_rows (GnmOOExport *state, Sheet const *sheet,
			     int from, int to, int row_length,
			     GnmPageBreaks *pb, G_GNUC_UNUSED GnmStyle **col_styles)
{
	int number_rows_rep;
	ColRowInfo const *last_ci;
	int i, j, next_to, style_rep;
	GnmStyleList *list;
	GnmRange r;

	if (from >= to)
		return;

	range_init_rows (&r, sheet, from, to - 1);
	list = sheet_style_get_range (sheet, &r);

	for (i = from; i < to; ) {
		if (gnm_page_breaks_get_break (pb, i) != GNM_PAGE_BREAK_NONE)
			gsf_xml_out_simple_element (state->xml,
						    TEXT "soft-page-break",
						    NULL);
		next_to = gnm_page_breaks_get_next_break (pb, i);
		if (next_to < from || next_to > to)
			next_to = to;

		gsf_xml_out_start_element (state->xml, TABLE "table-row");
		last_ci = sheet_row_get (sheet, i);
		write_row_style (state, last_ci, sheet);
		style_rep = write_styled_cells (state, sheet, i - from, row_length,
						next_to - i, list) - 1;
		gsf_xml_out_end_element (state->xml); /* table-row */
		i++;

		if (style_rep <= 0)
			continue;

		if (i + style_rep < next_to)
			next_to = i + style_rep;

		number_rows_rep = 1;
		last_ci = sheet_row_get (sheet, i);
		for (j = i + 1; j < next_to; j++) {
			ColRowInfo const *this_ci = sheet_row_get (sheet, j);

			if (colrow_equal (last_ci, this_ci))
				number_rows_rep++;
		}

		gsf_xml_out_start_element (state->xml, TABLE "table-row");
		write_row_style (state, last_ci, sheet);
		if (number_rows_rep > 1)
			gsf_xml_out_add_int (state->xml, TABLE "number-rows-repeated",
					     number_rows_rep);
		write_styled_cells (state, sheet, i - from, row_length, 0, list);
		gsf_xml_out_end_element (state->xml); /* table-row */

		i += number_rows_rep;
	}
	style_list_free (list);
}

static GSList *
odf_sheet_objects_get (Sheet const *sheet, GnmCellPos const *pos)
{
	GSList *res = NULL;
	GSList *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next ) {
		SheetObject *so = SHEET_OBJECT (ptr->data);
		SheetObjectAnchor const *anchor = sheet_object_get_anchor (so);
		if (gnm_cellpos_equal (&anchor->cell_bound.start, pos))
			res = g_slist_prepend (res, so);
	}
	return res;
}

static void
odf_write_content_rows (GnmOOExport *state, Sheet const *sheet, int from, int to,
			G_GNUC_UNUSED int col_from, G_GNUC_UNUSED int col_to, int row_length,
			GSList **sheet_merges, GnmPageBreaks *pb, G_GNUC_UNUSED GnmStyle **col_styles)
{
	int col, row;

	for (row = from; row < to; row++) {
		ColRowInfo const *ci = sheet_row_get (sheet, row);
		GnmStyle const *null_style = NULL;
		int null_cell = 0;
		int covered_cell = 0;
		GnmCellPos pos;

		pos.row = row;

		if (gnm_page_breaks_get_break (pb, row) != GNM_PAGE_BREAK_NONE)
			gsf_xml_out_simple_element (state->xml,
						    TEXT "soft-page-break",
						    NULL);

		gsf_xml_out_start_element (state->xml, TABLE "table-row");
		write_row_style (state, ci, sheet);

		for (col = 0; col < row_length; col++) {
			GnmCell *current_cell = sheet_cell_get (sheet, col, row);
			GnmRange const	*merge_range;
			GSList *objects;
			GnmStyle const *this_style;

			pos.col = col;
			merge_range = gnm_sheet_merge_is_corner (sheet, &pos);

			if (odf_cell_is_covered (sheet, current_cell, col, row,
						merge_range, sheet_merges)) {
				odf_write_empty_cell (state, null_cell, null_style, NULL);
				null_cell = 0;
				covered_cell++;
				continue;
			}

			objects = odf_sheet_objects_get (sheet, &pos);

			if ((merge_range == NULL) && (objects == NULL) &&
			    gnm_cell_is_empty (current_cell) &&
			    NULL == gnm_style_get_hlink
			    ((this_style = sheet_style_get (sheet, pos.col, pos.row)))) {
				if ((null_cell == 0) || (null_style == this_style)) {
					null_style = this_style;
					if (covered_cell > 0)
						odf_write_covered_cell (state, &covered_cell);
					null_cell++;
				} else {
					odf_write_empty_cell (state, null_cell, null_style, NULL);
					null_style = this_style;
					null_cell = 1;
				}
				continue;
			}

			odf_write_empty_cell (state, null_cell, null_style, NULL);
			null_cell = 0;
			if (covered_cell > 0)
				odf_write_covered_cell (state, &covered_cell);
			odf_write_cell (state, current_cell, merge_range, objects);

			g_slist_free (objects);

		}
		odf_write_empty_cell (state, null_cell, null_style, NULL);
		null_cell = 0;
		if (covered_cell > 0)
			odf_write_covered_cell (state, &covered_cell);

		gsf_xml_out_end_element (state->xml);   /* table-row */
	}


}

static void
odf_write_sheet (GnmOOExport *state)
{
	Sheet const *sheet = state->sheet;
	int max_cols = gnm_sheet_get_max_cols (sheet);
	int max_rows = gnm_sheet_get_max_rows (sheet);
	GnmStyle **col_styles = g_new0 (GnmStyle *, max_cols);
	GnmRange extent, style_extent;
	GSList *sheet_merges = NULL;
	GnmPageBreaks *pb = sheet->print_info->page_breaks.v;
	extent = sheet_get_extent (sheet, FALSE);

	style_extent = extent;
	/* We only want to get the common column style */
	sheet_style_get_extent (sheet, &style_extent, col_styles);

	/* ODF does not allow us to mark soft page breaks between columns */
	odf_write_formatted_columns (state, sheet, col_styles, 0, max_cols);

	odf_write_styled_empty_rows (state, sheet, 0, extent.start.row,
				     max_cols, pb, col_styles);
	odf_write_content_rows (state, sheet,
				extent.start.row, extent.end.row + 1,
				extent.start.col, extent.end.col + 1,
				max_cols, &sheet_merges, pb, col_styles);
	odf_write_styled_empty_rows (state, sheet, extent.end.row + 1, max_rows,
				     max_cols, pb, col_styles);

	go_slist_free_custom (sheet_merges, g_free);
	g_free (col_styles);

}

static char const *
odf_write_sheet_controls_get_id (GnmOOExport *state, SheetObject *so)
{
	char *id = g_strdup_printf ("CTRL%.4i",g_hash_table_size (state->controls));
	g_hash_table_replace (state->controls, so, id);
	return id;
}

static void
odf_write_sheet_control_content (GnmOOExport *state, GnmExprTop const *texpr)
{
	if (texpr && gnm_expr_top_is_rangeref (texpr)) {
		char *link = NULL;
		GnmParsePos pp;

		parse_pos_init_sheet (&pp, state->sheet);
		link = gnm_expr_top_as_string (texpr, &pp, state->conv);

		if (get_gsf_odf_version () > 101)
			gsf_xml_out_add_cstr (state->xml,
					      FORM "source-cell-range",
					      odf_strip_brackets (link));
		else
			gsf_xml_out_add_cstr (state->xml,
					      GNMSTYLE "source-cell-range",
					      odf_strip_brackets (link));
		g_free (link);
		gnm_expr_top_unref (texpr);
	}
}

static void
odf_write_sheet_control_linked_cell (GnmOOExport *state, GnmExprTop const *texpr)
{
	if (texpr && gnm_expr_top_is_rangeref (texpr)) {
		char *link = NULL;
		GnmParsePos pp;

		parse_pos_init_sheet (&pp, state->sheet);
		link = gnm_expr_top_as_string (texpr, &pp, state->conv);

		if (get_gsf_odf_version () > 101)
			gsf_xml_out_add_cstr (state->xml, FORM "linked-cell",
					      odf_strip_brackets (link));
		else
			gsf_xml_out_add_cstr (state->xml, GNMSTYLE "linked-cell",
					      odf_strip_brackets (link));
		g_free (link);
		gnm_expr_top_unref (texpr);
	}
}

static void
odf_sheet_control_start_element (GnmOOExport *state, SheetObject *so,
				 char const *element)
{
	char const *id = odf_write_sheet_controls_get_id (state, so);
	gsf_xml_out_start_element (state->xml, element);
	gsf_xml_out_add_cstr (state->xml, XML "id", id);
	gsf_xml_out_add_cstr (state->xml, FORM "id", id);

}

static void
odf_write_sheet_control_scrollbar (GnmOOExport *state, SheetObject *so,
				   char const *implementation)
{
	GtkAdjustment *adj = sheet_widget_adjustment_get_adjustment (so);
	GnmExprTop const *texpr = sheet_widget_adjustment_get_link (so);

	odf_sheet_control_start_element (state, so, FORM "value-range");

	if (implementation != NULL)
		gsf_xml_out_add_cstr (state->xml,
				      FORM "control-implementation",
				      implementation);
	gsf_xml_out_add_cstr (state->xml, FORM "orientation",
			      sheet_widget_adjustment_get_horizontal (so) ?
			      "horizontal" : "vertical");
	gsf_xml_out_add_float (state->xml, FORM "value",
		       gtk_adjustment_get_value (adj), -1);
	gsf_xml_out_add_float (state->xml, FORM "min-value",
		       gtk_adjustment_get_lower (adj), -1);
	gsf_xml_out_add_float (state->xml, FORM "max-value",
		       gtk_adjustment_get_upper (adj), -1);
	gsf_xml_out_add_int (state->xml, FORM "step-size",
			     (int)(gtk_adjustment_get_step_increment (adj) + 0.5));
	gsf_xml_out_add_int (state->xml, FORM "page-step-size",
			     (int)(gtk_adjustment_get_page_increment (adj) + 0.5));
	/* OOo fails to import this control, but adding its control-implementation */
	/* crashes OOo */
/* 	gsf_xml_out_add_cstr (state->xml, FORM "control-implementation",  */
/* 			      OOO "com.sun.star.form.component.ScrollBar"); */

	odf_write_sheet_control_linked_cell (state, texpr);
	gsf_xml_out_end_element (state->xml); /* form:value-range */
}

static void
odf_write_sheet_control_checkbox (GnmOOExport *state, SheetObject *so)
{
	GnmExprTop const *texpr = sheet_widget_checkbox_get_link (so);
	char *label = NULL;

	g_object_get (G_OBJECT (so), "text", &label, NULL);

	odf_sheet_control_start_element (state, so, FORM "checkbox");

	gsf_xml_out_add_cstr (state->xml, FORM "label", label);

	odf_write_sheet_control_linked_cell (state, texpr);

	gsf_xml_out_end_element (state->xml); /* form:checkbox */

	g_free (label);
}

static void
odf_write_sheet_control_frame (GnmOOExport *state, SheetObject *so)
{
	char *label = NULL;

	g_object_get (G_OBJECT (so), "text", &label, NULL);

	odf_sheet_control_start_element (state, so, FORM "generic-control");
	gsf_xml_out_add_cstr_unchecked (state->xml,
					FORM "control-implementation",
					GNMSTYLE "frame");

	gsf_xml_out_start_element (state->xml, FORM "properties");
	gsf_xml_out_start_element (state->xml, FORM "property");

	gsf_xml_out_add_cstr_unchecked (state->xml, FORM "property-name", GNMSTYLE "label");
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "value-type", "string");
	gsf_xml_out_add_cstr (state->xml, OFFICE "string-value", label);
	gsf_xml_out_end_element (state->xml); /* form:property */
	gsf_xml_out_end_element (state->xml); /* form:properties */

	gsf_xml_out_end_element (state->xml); /* form:generic-control */

	g_free (label);
}

static void
odf_write_sheet_control_list (GnmOOExport *state, SheetObject *so,
			      char const *element)
{
	GnmExprTop const *texpr = sheet_widget_list_base_get_result_link (so);
	gboolean as_index = sheet_widget_list_base_result_type_is_index (so);

	odf_sheet_control_start_element (state, so, element);

	odf_write_sheet_control_linked_cell (state, texpr);

	texpr = sheet_widget_list_base_get_content_link (so);
	odf_write_sheet_control_content (state, texpr);

	if (get_gsf_odf_version () > 101)
		gsf_xml_out_add_cstr_unchecked
			(state->xml, FORM "list-linkage-type",
			 as_index ? "selection-indexes" : "selection");
	else if (state->with_extension)
		gsf_xml_out_add_cstr_unchecked
			(state->xml, GNMSTYLE "list-linkage-type",
			 as_index ? "selection-indices" : "selection");

	gsf_xml_out_add_int (state->xml, FORM "bound-column", 1);
	gsf_xml_out_end_element (state->xml); /* form:checkbox */
}

static void
odf_write_sheet_control_radio_button (GnmOOExport *state, SheetObject *so)
{
	GnmExprTop const *texpr = sheet_widget_radio_button_get_link (so);
	char *label = NULL;
	GnmValue *val = NULL;

	g_object_get (G_OBJECT (so), "text", &label, "value", &val, NULL);

	odf_sheet_control_start_element (state, so, FORM "radio");

	gsf_xml_out_add_cstr (state->xml, FORM "label", label);

	if (val != NULL) {
		switch (val->type) {
		case VALUE_EMPTY:
			break;
		case VALUE_BOOLEAN:
			if (state->with_extension)
				gsf_xml_out_add_cstr_unchecked
					(state->xml,
					 GNMSTYLE "value-type",
					 "boolean");
			odf_add_bool (state->xml, FORM "value",
				      value_get_as_bool (val, NULL));
			break;
		case VALUE_FLOAT: {
			GString *str = g_string_new (NULL);
			if (state->with_extension)
				gsf_xml_out_add_cstr_unchecked
					(state->xml,
					 GNMSTYLE "value-type",
					 "float");
			value_get_as_gstring (val, str, state->conv);
			gsf_xml_out_add_cstr (state->xml, FORM "value",
					      str->str);
			g_string_free (str, TRUE);
			break;
		}
		case VALUE_ERROR:
		case VALUE_STRING:
			if (state->with_extension)
				gsf_xml_out_add_cstr_unchecked
					(state->xml,
					 GNMSTYLE "value-type",
					 "string");
			gsf_xml_out_add_cstr (state->xml,
					      FORM "value",
					      value_peek_string (val));
			break;
		case VALUE_CELLRANGE:
		case VALUE_ARRAY:
		default:
			break;
		}
	}

	odf_write_sheet_control_linked_cell (state, texpr);

	gsf_xml_out_end_element (state->xml); /* form:checkbox */

	g_free (label);
}

static void
odf_write_sheet_control_button (GnmOOExport *state, SheetObject *so)
{
	GnmExprTop const *texpr = sheet_widget_button_get_link (so);
	char *label = NULL;

	g_object_get (G_OBJECT (so), "text", &label, NULL);

	odf_sheet_control_start_element (state, so, FORM "button");
	gsf_xml_out_add_cstr (state->xml, FORM "label", label);
	gsf_xml_out_add_cstr_unchecked (state->xml, FORM "button-type", "push");

	if (texpr != NULL ) {
		char *link = NULL, *name = NULL;
		GnmParsePos pp;

		parse_pos_init_sheet (&pp, state->sheet);
		link = gnm_expr_top_as_string (texpr, &pp, state->conv);

		gsf_xml_out_start_element (state->xml, OFFICE "event-listeners");

		gsf_xml_out_start_element (state->xml, SCRIPT "event-listener");
		gsf_xml_out_add_cstr_unchecked (state->xml, SCRIPT "event-name",
						"dom:mousedown");
		gsf_xml_out_add_cstr_unchecked (state->xml, SCRIPT "language",
						GNMSTYLE "short-macro");
		name = g_strdup_printf ("set-to-TRUE:%s", odf_strip_brackets (link));
		gsf_xml_out_add_cstr (state->xml, SCRIPT "macro-name", name);
		g_free (name);
		gsf_xml_out_end_element (state->xml); /* script:event-listener */

		gsf_xml_out_start_element (state->xml, SCRIPT "event-listener");
		gsf_xml_out_add_cstr_unchecked (state->xml, SCRIPT "event-name",
						"dom:mouseup");
		gsf_xml_out_add_cstr_unchecked (state->xml, SCRIPT "language",
						GNMSTYLE "short-macro");
		name = g_strdup_printf ("set-to-FALSE:%s", odf_strip_brackets (link));
		gsf_xml_out_add_cstr (state->xml, SCRIPT "macro-name", name);
		g_free (name);
		gsf_xml_out_end_element (state->xml); /* script:event-listener */

		gsf_xml_out_end_element (state->xml); /* office:event-listeners */

		g_free (link);
		gnm_expr_top_unref (texpr);

	}
	gsf_xml_out_end_element (state->xml); /* form:checkbox */
}

static void
odf_write_sheet_controls (GnmOOExport *state)
{
	Sheet const *sheet = state->sheet;
	GSList *objects = sheet->sheet_objects, *l;

	gsf_xml_out_start_element (state->xml, OFFICE "forms");
	odf_add_bool (state->xml, FORM "automatic-focus", FALSE);
	odf_add_bool (state->xml, FORM "apply-design-mode", FALSE);
	gsf_xml_out_start_element (state->xml, FORM "form");

	for (l = objects; l != NULL; l = l->next) {
		SheetObject *so = l->data;

		if (GNM_IS_SOW_SCROLLBAR (so))
			odf_write_sheet_control_scrollbar
				(state, so, GNMSTYLE "scrollbar");
		else if (GNM_IS_SOW_SLIDER (so))
			odf_write_sheet_control_scrollbar
				(state, so, GNMSTYLE "slider");
		else if (GNM_IS_SOW_SPINBUTTON (so))
			odf_write_sheet_control_scrollbar
				(state, so, GNMSTYLE "spinbutton");
		else if (GNM_IS_SOW_CHECKBOX (so))
			odf_write_sheet_control_checkbox (state, so);
		else if (GNM_IS_SOW_RADIO_BUTTON (so))
			odf_write_sheet_control_radio_button (state, so);
		else if (GNM_IS_SOW_LIST (so))
			odf_write_sheet_control_list (state, so,
						      FORM "listbox");
		else if (GNM_IS_SOW_COMBO (so))
			odf_write_sheet_control_list (state, so,
						      FORM "combobox");
		else if (GNM_IS_SOW_BUTTON (so))
			odf_write_sheet_control_button (state, so);
		else if (GNM_IS_SOW_FRAME (so))
			odf_write_sheet_control_frame (state, so);
	}

	gsf_xml_out_end_element (state->xml); /* form:form */
	gsf_xml_out_end_element (state->xml); /* office:forms */
}

static void
odf_write_filter_cond (GnmOOExport *state, GnmFilter const *filter, int i)
{
	GnmFilterCondition const *cond = gnm_filter_get_condition (filter, i);
	char const *op, *type = NULL;
	GString *val_str = NULL;

	if (cond == NULL)
		return;

	switch (cond->op[0]) {
	case GNM_FILTER_OP_EQUAL:	op = "="; break;
	case GNM_FILTER_OP_GT:		op = ">"; break;
	case GNM_FILTER_OP_LT:		op = "<"; break;
	case GNM_FILTER_OP_GTE:		op = ">="; break;
	case GNM_FILTER_OP_LTE:		op = "<="; break;
	case GNM_FILTER_OP_NOT_EQUAL:	op = "!="; break;
	case GNM_FILTER_OP_MATCH:	op = "match"; break;
	case GNM_FILTER_OP_NO_MATCH:	op = "!match"; break;

	case GNM_FILTER_OP_BLANKS:		op = "empty"; break;
	case GNM_FILTER_OP_NON_BLANKS:		op = "!empty"; break;
	case GNM_FILTER_OP_TOP_N:		op = "top values"; break;
	case GNM_FILTER_OP_BOTTOM_N:		op = "bottom values"; break;
	case GNM_FILTER_OP_TOP_N_PERCENT:	op = "top percent"; break;
	case GNM_FILTER_OP_BOTTOM_N_PERCENT:	op = "bottom percent"; break;
	/* remainder are not supported in ODF */
	default :
		return;
	}

	if (GNM_FILTER_OP_TYPE_BUCKETS == (cond->op[0] & GNM_FILTER_OP_TYPE_MASK)) {
		val_str = g_string_new (NULL);
		type = "number";
		g_string_printf (val_str, "%g", cond->count);
	} else if (GNM_FILTER_OP_TYPE_BLANKS  != (cond->op[0] & GNM_FILTER_OP_TYPE_MASK)) {
		val_str = g_string_new (NULL);
		type = VALUE_IS_FLOAT (cond->value[0]) ? "number" : "text";
		value_get_as_gstring (cond->value[0], val_str, state->conv);
	}

	gsf_xml_out_start_element (state->xml, TABLE "filter-condition");
	gsf_xml_out_add_int (state->xml, TABLE "field-number", i);
	if (NULL != type && val_str != NULL) {
		gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "data-type", type);
		gsf_xml_out_add_cstr (state->xml, TABLE "value", val_str->str);
	}
	gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "operator", op);
	gsf_xml_out_end_element (state->xml); /* </table:filter-condition> */

	if (val_str)
		g_string_free (val_str, TRUE);
}

static void
odf_write_autofilter (GnmOOExport *state, GnmFilter const *filter)
{
	GString *buf;
	unsigned i;

	gsf_xml_out_start_element (state->xml, TABLE "database-range");

	/* manually create a ref string with no '[]' bracing */
	buf = g_string_new (filter->sheet->name_quoted);
	g_string_append_c (buf, '.');
	g_string_append	  (buf, cellpos_as_string (&filter->r.start));
	g_string_append_c (buf, ':');
	g_string_append   (buf, filter->sheet->name_quoted);
	g_string_append_c (buf, '.');
	g_string_append   (buf, cellpos_as_string (&filter->r.end));
	gsf_xml_out_add_cstr (state->xml, TABLE "target-range-address", buf->str);
	g_string_free (buf, TRUE);

	odf_add_bool (state->xml, TABLE "display-filter-buttons", TRUE);

	if (filter->is_active) {
		gsf_xml_out_start_element (state->xml, TABLE "filter");
		for (i = 0 ; i < filter->fields->len ; i++)
			odf_write_filter_cond (state, filter, i);
		gsf_xml_out_end_element (state->xml); /* </table:filter> */
	}

	gsf_xml_out_end_element (state->xml); /* </table:database-range> */
}

static void
odf_validation_general_attributes (GnmOOExport *state, GnmValidation const *val)
{
	char *name = g_strdup_printf ("VAL-%p", val);
	
	gsf_xml_out_add_cstr (state->xml, TABLE "name", name);
	g_free (name);
	odf_add_bool (state->xml,  TABLE "allow-empty-cell", val->allow_blank);
	gsf_xml_out_add_cstr (state->xml,  TABLE "display-list", 
			      val->use_dropdown ? "unsorted" : "none");
}

static void
odf_validation_in_list (GnmOOExport *state, GnmValidation const *val, 
			Sheet *sheet, GnmStyleRegion const *sr)
{
	GnmExprTop const *texpr;
	GnmParsePos pp;
	char *formula;
	GnmCellRef ref;
	GString *str;

	gnm_cellref_init (&ref, sheet, 
			  sr->range.start.col, 
			  sr->range.start.row, TRUE);
	texpr =  gnm_expr_top_new (gnm_expr_new_cellref (&ref));
	parse_pos_init (&pp, (Workbook *)state->wb, sheet, 
			sr->range.start.col, 
			sr->range.start.row);
	formula = gnm_expr_top_as_string (texpr, &pp, state->conv);
	gsf_xml_out_add_cstr (state->xml, TABLE "base-cell-address", 
			      odf_strip_brackets (formula));
	g_free (formula);
	gnm_expr_top_unref (texpr);

	/* Note that this is really not valid ODF1.1 but will be valid in ODF1.2 */
	formula = gnm_expr_top_as_string (val->texpr[0], &pp, state->conv);
	str = g_string_new ("of:cell-content-is-in-list(");
	g_string_append (str, formula);
	g_string_append_c (str, ')');

	g_free (formula);
	gsf_xml_out_add_cstr (state->xml, TABLE "condition", str->str);
	g_string_free (str, TRUE);
}

static void
odf_print_spreadsheet_content_validations (GnmOOExport *state)
{
	gboolean element_written = FALSE;
	int i;
	
	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet *sheet = workbook_sheet_by_index (state->wb, i);
		GnmStyleList *list, *l;

		list = sheet_style_collect_validations (sheet, NULL);

		for (l = list; l != NULL; l = l->next) {
			GnmStyleRegion const *sr  = l->data;
			GnmValidation const *val = gnm_style_get_validation (sr->style);

			if (val->type == VALIDATION_TYPE_IN_LIST) {
				if (!element_written) {
					gsf_xml_out_start_element 
						(state->xml, TABLE "content-validations");
					element_written = TRUE;
				}
				gsf_xml_out_start_element (state->xml, 
							   TABLE "content-validation");
				odf_validation_general_attributes (state, val);
				odf_validation_in_list (state, val, sheet, sr);
				gsf_xml_out_end_element (state->xml); 
				/* </table:content-validation> */
			}
		}

		style_list_free (list);
	}

	if (element_written)
		gsf_xml_out_end_element (state->xml); /* </table:content-validations> */

}

static void
odf_print_spreadsheet_content_prelude (GnmOOExport *state)
{
	gsf_xml_out_start_element (state->xml, TABLE "calculation-settings");
	gsf_xml_out_add_int (state->xml, TABLE "null-year", 1930);
	odf_add_bool (state->xml, TABLE "automatic-find-labels", FALSE);
	odf_add_bool (state->xml, TABLE "case-sensitive", FALSE);
	odf_add_bool (state->xml, TABLE "precision-as-shown", FALSE);
	odf_add_bool (state->xml, TABLE "search-criteria-must-apply-to-whole-cell", TRUE);
	odf_add_bool (state->xml, TABLE "use-regular-expressions", FALSE);
	if (get_gsf_odf_version () > 101)
		odf_add_bool (state->xml, TABLE "use-wildcards", FALSE);
	gsf_xml_out_start_element (state->xml, TABLE "null-date");
	if (go_date_convention_base (workbook_date_conv (state->wb)) == 1900)
		/* As encouraged by the OpenFormula definition we "compensate" here. */
		gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "date-value", "1899-12-30");
	else
		gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "date-value", "1904-1-1");
	gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "value-type", "date");
	gsf_xml_out_end_element (state->xml); /* </table:null-date> */
	gsf_xml_out_start_element (state->xml, TABLE "iteration");
	gsf_xml_out_add_float (state->xml, TABLE "maximum-difference",
			       state->wb->iteration.tolerance, -1);
	gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "status",
					state->wb->iteration.enabled ?  "enable" : "disable");
	gsf_xml_out_add_int (state->xml, TABLE "steps", state->wb->iteration.max_number);
	gsf_xml_out_end_element (state->xml); /* </table:iteration> */
	gsf_xml_out_end_element (state->xml); /* </table:calculation-settings> */

	odf_print_spreadsheet_content_validations (state);
}

static void
odf_write_named_expression (gpointer key, GnmNamedExpr *nexpr, GnmOOExport *state)
{
	char const *name;
	gboolean is_range;
	char *formula;
	GnmCellRef ref;
	GnmExprTop const *texpr;
	Sheet *sheet;
	
	g_return_if_fail (nexpr != NULL);

	sheet = nexpr->pos.sheet;
        if (sheet == NULL)
		sheet = workbook_sheet_by_index (state->wb, 0);

	name = expr_name_name (nexpr);
	is_range = nexpr->texpr && !expr_name_is_placeholder (nexpr) 
		&& gnm_expr_top_is_rangeref (nexpr->texpr);

	if (is_range) {
		gsf_xml_out_start_element (state->xml, TABLE "named-range");
		gsf_xml_out_add_cstr (state->xml, TABLE "name", name);
		
		formula = gnm_expr_top_as_string (nexpr->texpr,
						  &nexpr->pos,
						  state->conv);
		gsf_xml_out_add_cstr (state->xml, TABLE "cell-range-address",
				      odf_strip_brackets (formula));
		g_free (formula);
		
		gnm_cellref_init (&ref, sheet, nexpr->pos.eval.col, 
				  nexpr->pos.eval.row, FALSE);
		texpr =  gnm_expr_top_new (gnm_expr_new_cellref (&ref));
		formula = gnm_expr_top_as_string (texpr, &nexpr->pos, state->conv);
		gsf_xml_out_add_cstr (state->xml,
				      TABLE "base-cell-address",
				      odf_strip_brackets (formula));
		g_free (formula);
		gnm_expr_top_unref (texpr);
		
		gsf_xml_out_add_cstr_unchecked
			(state->xml, TABLE "range-usable-as", 
			 "print-range filter repeat-row repeat-column");
		
		if (nexpr->pos.sheet != NULL && state->with_extension 
		    && (get_gsf_odf_version () < 102))
			gsf_xml_out_add_cstr (state->xml, GNMSTYLE "scope", 
					      nexpr->pos.sheet->name_unquoted);

		gsf_xml_out_end_element (state->xml); /* </table:named-range> */
	} else {
		gsf_xml_out_start_element 
			(state->xml, TABLE "named-expression");
		gsf_xml_out_add_cstr (state->xml, TABLE "name", name);
		
		formula = gnm_expr_top_as_string (nexpr->texpr,
						  &nexpr->pos,
						  state->conv);
		if (get_gsf_odf_version () > 101) {
			char *eq_formula = g_strdup_printf ("of:=%s", formula);
			gsf_xml_out_add_cstr (state->xml,  TABLE "expression", eq_formula);
			g_free (eq_formula);
		} else
			gsf_xml_out_add_cstr (state->xml,  TABLE "expression", formula);
		g_free (formula);
		
		gnm_cellref_init (&ref, sheet, nexpr->pos.eval.col, 
				  nexpr->pos.eval.row, FALSE);
		texpr =  gnm_expr_top_new (gnm_expr_new_cellref (&ref));
		formula = gnm_expr_top_as_string (texpr, &nexpr->pos, state->conv);
		gsf_xml_out_add_cstr (state->xml,
				      TABLE "base-cell-address",
				      odf_strip_brackets (formula));
		g_free (formula);
		gnm_expr_top_unref (texpr);

		if (nexpr->pos.sheet != NULL && state->with_extension 
		    && (get_gsf_odf_version () < 102))
			gsf_xml_out_add_cstr (state->xml, GNMSTYLE "scope", 
					      nexpr->pos.sheet->name_unquoted);
		
		gsf_xml_out_end_element (state->xml); /* </table:named-expression> */
	}
}


static void
odf_write_content (GnmOOExport *state, GsfOutput *child)
{
	int i;
	int graph_n = 1;
	int image_n = 1;
	gboolean has_autofilters = FALSE;

	state->xml = gsf_xml_out_new (child);
	gsf_xml_out_set_doc_type (state->xml, "\n");
	gsf_xml_out_start_element (state->xml, OFFICE "document-content");

	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version",
					get_gsf_odf_version_string ());

	gsf_xml_out_simple_element (state->xml, OFFICE "scripts", NULL);

	gsf_xml_out_start_element (state->xml, OFFICE "font-face-decls");
	gsf_xml_out_end_element (state->xml); /* </office:font-face-decls> */

	gsf_xml_out_start_element (state->xml, OFFICE "automatic-styles");
	odf_write_table_styles (state);
	odf_write_character_styles (state);
	odf_write_cell_styles (state);
	odf_write_column_styles (state);
	odf_write_row_styles (state);
	odf_write_sheet_object_styles (state);
	gsf_xml_out_end_element (state->xml); /* </office:automatic-styles> */

	gsf_xml_out_start_element (state->xml, OFFICE "body");
	gsf_xml_out_start_element (state->xml, OFFICE "spreadsheet");

	odf_print_spreadsheet_content_prelude (state);

	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet *sheet = workbook_sheet_by_index (state->wb, i);
		char *style_name;
		GnmRange    *p_area;
		GSList *l, *graphs, *images;

		state->sheet = sheet;

		graphs = sheet_objects_get (sheet, NULL, SHEET_OBJECT_GRAPH_TYPE);
		for (l = graphs; l != NULL; l = l->next)
			g_hash_table_insert (state->graphs, l->data,
					     g_strdup_printf ("Graph%i", graph_n++));
		g_slist_free (graphs);

		images = sheet_objects_get (sheet, NULL, SHEET_OBJECT_IMAGE_TYPE);
		for (l = images; l != NULL; l = l->next)
			g_hash_table_insert (state->images, l->data,
					     g_strdup_printf ("Image%i", image_n++));
		g_slist_free (images);

		gsf_xml_out_start_element (state->xml, TABLE "table");
		gsf_xml_out_add_cstr (state->xml, TABLE "name", sheet->name_unquoted);

		style_name = table_style_name (sheet);
		gsf_xml_out_add_cstr (state->xml, TABLE "style-name", style_name);
		g_free (style_name);

		p_area  = sheet_get_nominal_printarea (sheet);

		if (p_area != NULL) {
			GnmValue *v = value_new_cellrange_r (sheet, p_area);
			GnmExprTop const *texpr;
			char *formula;
			GnmParsePos pp;
			GnmCellRef *a, *b;

			a = &v->v_range.cell.a;
			b = &v->v_range.cell.b;
			a->col_relative = b->col_relative = TRUE;
			a->row_relative = b->row_relative = TRUE;

			texpr = gnm_expr_top_new_constant (v);

			g_free (p_area);
			parse_pos_init_sheet (&pp, sheet);
			formula = gnm_expr_top_as_string (texpr,
							  &pp,
							  state->conv);
			gnm_expr_top_unref (texpr);
			gsf_xml_out_add_cstr (state->xml, TABLE "print-ranges",
					      odf_strip_brackets (formula));
			g_free (formula);
		}

		odf_write_sheet_controls (state);
		odf_write_sheet (state);
		if (get_gsf_odf_version () > 101 && sheet->names) {
			gsf_xml_out_start_element (state->xml, TABLE "named-expressions");
			gnm_sheet_foreach_name (sheet,
						(GHFunc)&odf_write_named_expression, state);
			gsf_xml_out_end_element (state->xml); /* </table:named-expressions> */
		}
		gsf_xml_out_end_element (state->xml); /* </table:table> */

		has_autofilters |= (sheet->filters != NULL);
		odf_update_progress (state, state->sheet_progress);
	}
	if (state->wb->names != NULL) {
		gsf_xml_out_start_element (state->xml, TABLE "named-expressions");
		workbook_foreach_name 
			(state->wb, (get_gsf_odf_version () > 101),
			 (GHFunc)&odf_write_named_expression, state);
		gsf_xml_out_end_element (state->xml); /* </table:named-expressions> */
	}
	if (has_autofilters) {
		gsf_xml_out_start_element (state->xml, TABLE "database-ranges");
		for (i = 0; i < workbook_sheet_count (state->wb); i++) {
			Sheet *sheet = workbook_sheet_by_index (state->wb, i);
			GSList *ptr;
			for (ptr = sheet->filters ; ptr != NULL ; ptr = ptr->next)
				odf_write_autofilter (state, ptr->data);
		}

		gsf_xml_out_end_element (state->xml); /* </table:database-ranges> */
	}

	gsf_xml_out_end_element (state->xml); /* </office:spreadsheet> */
	gsf_xml_out_end_element (state->xml); /* </office:body> */

	gsf_xml_out_end_element (state->xml); /* </office:document-content> */
	g_object_unref (state->xml);
	state->xml = NULL;
}

/*****************************************************************************/

static void
odf_write_xl_style (char const *xl, char const *name, GnmOOExport *state, int i)
{
	GOFormat *format;
	if (xl == NULL)
		xl = "General";
	format = go_format_new_from_XL (xl);
	go_format_output_to_odf (state->xml, format, i, name, state->with_extension);
	go_format_unref (format);
}

static void
odf_write_this_xl_style (char const *xl, char const *name, GnmOOExport *state)
{
	odf_write_xl_style (xl, name, state, 0);
}

static void
odf_write_this_xl_style_neg (char const *xl, char const *name, GnmOOExport *state)
{
	odf_write_xl_style (xl, name, state, 1);
}

static void
odf_write_this_xl_style_zero (char const *xl, char const *name, GnmOOExport *state)
{
	odf_write_xl_style (xl, name, state, 2);
}

static gboolean
odf_write_map (GnmOOExport *state, char const *xl, int i)
{
	GHashTable *xl_styles;
	GOFormat *format = go_format_new_from_XL (xl);
	char *condition = go_format_odf_style_map (format, i);
	go_format_unref (format);
	if (condition == NULL)
		return FALSE;
	switch (i) {
	case 0:
		xl_styles = state->xl_styles;
		break;
	case 1:
		xl_styles = state->xl_styles_neg;
		break;
	default:
		xl_styles = state->xl_styles_zero;
		break;

	}
	gsf_xml_out_start_element (state->xml, STYLE "map");
	gsf_xml_out_add_cstr (state->xml, STYLE "condition", condition);
	gsf_xml_out_add_cstr (state->xml, STYLE "apply-style-name",
			      g_hash_table_lookup (xl_styles, xl));
	gsf_xml_out_end_element (state->xml); /* </style:map> */
	g_free (condition);
	return TRUE;
}

static void
odf_write_this_conditional_xl_style (char const *xl, char const *name, GnmOOExport *state)
{
	int i = 0;

	gsf_xml_out_start_element (state->xml, NUMBER "number-style");
	gsf_xml_out_add_cstr (state->xml, STYLE "name", name);
	while (odf_write_map (state, xl, i++)) {}
	gsf_xml_out_end_element (state->xml); /* </number:number-style> */
}

static void
odf_write_styles (GnmOOExport *state, GsfOutput *child)
{
	int i;

	state->xml = gsf_xml_out_new (child);
	gsf_xml_out_start_element (state->xml, OFFICE "document-styles");
	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version",
					get_gsf_odf_version_string ());
	gsf_xml_out_start_element (state->xml, OFFICE "styles");

	g_hash_table_foreach (state->xl_styles, (GHFunc) odf_write_this_xl_style, state);
	g_hash_table_foreach (state->xl_styles_neg, (GHFunc) odf_write_this_xl_style_neg, state);
	g_hash_table_foreach (state->xl_styles_zero, (GHFunc) odf_write_this_xl_style_zero, state);
	g_hash_table_foreach (state->xl_styles_conditional, (GHFunc) odf_write_this_conditional_xl_style, state);

	g_hash_table_foreach (state->named_cell_styles, (GHFunc) odf_save_this_style_with_name, state);

	if (state->default_style != NULL) {
		gsf_xml_out_start_element (state->xml, STYLE "default-style");
		gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "family", "table-cell");
		odf_write_style (state, state->default_style, TRUE);
		gsf_xml_out_end_element (state->xml); /* </style:default-style */
	}
	if (state->column_default != NULL) {
		gsf_xml_out_start_element (state->xml, STYLE "default-style");
		gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "family", "table-column");
		odf_write_col_style (state, state->column_default);
		gsf_xml_out_end_element (state->xml); /* </style:default-style */
	}
	if (state->row_default != NULL) {
		gsf_xml_out_start_element (state->xml, STYLE "default-style");
		gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "family", "table-row");
		odf_write_row_style (state, state->row_default);
		gsf_xml_out_end_element (state->xml); /* </style:default-style */
	}

	g_hash_table_foreach (state->graph_dashes, (GHFunc) odf_write_dash_info, state);
	g_hash_table_foreach (state->graph_hatches, (GHFunc) odf_write_hatch_info, state);
	g_hash_table_foreach (state->graph_gradients, (GHFunc) odf_write_gradient_info, state);
	g_hash_table_foreach (state->graph_fill_images, (GHFunc) odf_write_fill_images_info, state);
	g_hash_table_foreach (state->arrow_markers, (GHFunc) odf_write_arrow_marker_info, state);

	g_hash_table_remove_all (state->graph_dashes);
	g_hash_table_remove_all (state->graph_hatches);
	g_hash_table_remove_all (state->graph_gradients);
	g_hash_table_remove_all (state->graph_fill_images);
	g_hash_table_remove_all (state->arrow_markers);

	gsf_xml_out_end_element (state->xml); /* </office:styles> */
	gsf_xml_out_end_element (state->xml); /* </office:document-styles> */

	g_object_unref (state->xml);
	state->xml = NULL;
}

/*****************************************************************************/

static void
odf_write_meta (GnmOOExport *state, GsfOutput *child)
{
	GsfXMLOut *xml = gsf_xml_out_new (child);
	GsfDocMetaData *meta = go_doc_get_meta_data (GO_DOC (state->wb));
	GValue *val = g_new0 (GValue, 1);
	GsfDocProp *prop = gsf_doc_meta_data_steal (meta, GSF_META_NAME_GENERATOR);

	g_value_init (val, G_TYPE_STRING);
	g_value_set_string (val, PACKAGE_NAME "/" VERSION);

	gsf_doc_meta_data_insert  (meta, g_strdup (GSF_META_NAME_GENERATOR), val);
	gsf_opendoc_metadata_write (xml, meta);
	gsf_doc_meta_data_remove (meta,GSF_META_NAME_GENERATOR);
	if (prop != NULL)
		gsf_doc_meta_data_store (meta, prop);
	g_object_unref (xml);
}

static void
odf_write_meta_graph (GnmOOExport *state, GsfOutput *child)
{
	GsfXMLOut *xml = gsf_xml_out_new (child);
	GsfDocMetaData *meta = gsf_doc_meta_data_new ();
	GValue *val = g_new0 (GValue, 1);

	g_value_init (val, G_TYPE_STRING);
	g_value_set_string (val, PACKAGE_NAME "/" VERSION);

	gsf_doc_meta_data_insert  (meta, g_strdup (GSF_META_NAME_GENERATOR), val);
	gsf_opendoc_metadata_write (xml, meta);

	g_object_unref (meta);
	g_object_unref (xml);
}
/*****************************************************************************/

static void
odf_write_fill_images_info (GOImage *image, char const *name, GnmOOExport *state)
{
	char const *display_name = go_image_get_name (image);
	char *href = g_strdup_printf ("Pictures/%s.png", name);

	gsf_xml_out_start_element (state->xml, DRAW "fill-image");
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "name", name);
	gsf_xml_out_add_cstr (state->xml, DRAW "display-name", display_name);
	gsf_xml_out_add_cstr_unchecked (state->xml, XLINK "type", "simple");
	gsf_xml_out_add_cstr_unchecked (state->xml, XLINK "show", "embed");
	gsf_xml_out_add_cstr_unchecked (state->xml, XLINK "actuate", "onLoad");
	gsf_xml_out_add_cstr (state->xml, XLINK "href", href);
	gsf_xml_out_end_element (state->xml); /* </draw:fill-image> */

	g_free (href);
}

static void
odf_write_arrow_marker_info (GOArrow const *arrow, char const *name, GnmOOExport *state)
{
	gsf_xml_out_start_element (state->xml, DRAW "marker");
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "name", name);

	if (state->with_extension) {
		gsf_xml_out_add_int (state->xml, GNMSTYLE "arrow-type", arrow->typ);
		gsf_xml_out_add_float (state->xml, GNMSTYLE "arrow-a", arrow->a, -1);
		gsf_xml_out_add_float (state->xml, GNMSTYLE "arrow-b", arrow->b, -1);
		gsf_xml_out_add_float (state->xml, GNMSTYLE "arrow-c", arrow->c, -1);
	}

	gsf_xml_out_add_cstr (state->xml, SVG "viewBox", "0 0 20 30");
	gsf_xml_out_add_cstr (state->xml, SVG "d", "m10 0-10 30h20z");

	gsf_xml_out_end_element (state->xml); /* </draw:marker> */
}

static void
odf_write_gradient_info (GOStyle const *style, char const *name, GnmOOExport *state)
{
	char *color;
	char const *type = "linear";
	int angle = 0;
	struct {
		unsigned int dir;
		char const *type;
		int angle;
	} gradients[] = {
		{GO_GRADIENT_N_TO_S,"linear", 180},
		{GO_GRADIENT_S_TO_N, "linear", 0},
		{GO_GRADIENT_N_TO_S_MIRRORED, "axial", 180},
		{GO_GRADIENT_S_TO_N_MIRRORED, "axial", 0},
		{GO_GRADIENT_W_TO_E, "linear", -90},
		{GO_GRADIENT_E_TO_W, "linear", 90},
		{GO_GRADIENT_W_TO_E_MIRRORED, "axial", -90},
		{GO_GRADIENT_E_TO_W_MIRRORED, "axial", 90},
		{GO_GRADIENT_NW_TO_SE, "linear", -135},
		{GO_GRADIENT_SE_TO_NW, "linear", 45},
		{GO_GRADIENT_NW_TO_SE_MIRRORED, "axial", -135 },
		{GO_GRADIENT_SE_TO_NW_MIRRORED, "axial", 45},
		{GO_GRADIENT_NE_TO_SW, "linear", 135},
		{GO_GRADIENT_SW_TO_NE, "linear", -45},
		{GO_GRADIENT_SW_TO_NE_MIRRORED, "axial", -45},
		{GO_GRADIENT_NE_TO_SW_MIRRORED, "axial", 135},
	};
	int i;

	gsf_xml_out_start_element (state->xml, DRAW "gradient");
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "name", name);

	color = odf_go_color_to_string (style->fill.pattern.back);
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "start-color", color);
	g_free (color);

	if (style->fill.gradient.brightness >= 0.0 && state->with_extension)
		gsf_xml_out_add_float (state->xml, GNMSTYLE "brightness",
				       style->fill.gradient.brightness, -1);

	color = odf_go_color_to_string (style->fill.pattern.fore);
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "end-color", color);
	g_free (color);

	for (i = 0; i < (int)G_N_ELEMENTS (gradients); i++) {
		if (gradients[i].dir == style->fill.gradient.dir) {
			type = gradients[i].type;
			angle = gradients[i].angle;
			break;
		}
	}
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "style", type);
	gsf_xml_out_add_int (state->xml, DRAW "angle", angle);

	gsf_xml_out_end_element (state->xml); /* </draw:gradient> */
}

static void
odf_write_hatch_info (GOPattern *pattern, char const *name, GnmOOExport *state)
{
	struct {
		unsigned int type;
		char const *style;
		int angle;
		double distance;
	} info[] = {
		{GO_PATTERN_GREY75, "double", 0, 1.0},
		{GO_PATTERN_GREY50, "double", 0, 2.0},
		{GO_PATTERN_GREY25, "double", 0, 3.0},
		{GO_PATTERN_GREY125, "double", 0, 4.0},
		{GO_PATTERN_GREY625, "double", 0, 5.0},
		{GO_PATTERN_HORIZ, "single", 0, 2.0},
		{GO_PATTERN_VERT, "single", 90, 2.0},
		{GO_PATTERN_REV_DIAG, "single", -45, 2.0},
		{GO_PATTERN_DIAG, "single", 45, 2.0},
		{GO_PATTERN_DIAG_CROSS, "double", 45, 2.0},
		{GO_PATTERN_THICK_DIAG_CROSS, "double", 45, 1.0},
		{GO_PATTERN_THIN_HORIZ, "single", 0, 3.0},
		{GO_PATTERN_THIN_VERT, "single", 90, 3.0},
		{GO_PATTERN_THIN_REV_DIAG, "single", -45, 3.0},
		{GO_PATTERN_THIN_DIAG, "single", 45, 3.0},
		{GO_PATTERN_THIN_HORIZ_CROSS, "double", 0, 3.0},
		{GO_PATTERN_THIN_DIAG_CROSS, "double", 45, 3.0},
		{GO_PATTERN_SMALL_CIRCLES, "triple", 0, 2.0},
		{GO_PATTERN_SEMI_CIRCLES, "triple", 45, 2.0},
		{GO_PATTERN_THATCH, "triple", 90, 2.0},
		{GO_PATTERN_LARGE_CIRCLES, "triple", 0, 3.0},
		{GO_PATTERN_BRICKS, "triple", 45, 3.0},
		{GO_PATTERN_MAX, "single", 0, 2.0}
	};
	char *color = odf_go_color_to_string (pattern->fore);
	int i;

	gsf_xml_out_start_element (state->xml, DRAW "hatch");
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "name", name);
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "display-name", name);
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "color", color);
	g_free (color);

	for (i = 0; info[i].type != GO_PATTERN_MAX; i++)
		if (info[i].type == pattern->pattern)
			break;

	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "style",
					info[i].style);
	odf_add_angle (state->xml, DRAW "rotation", info[i].angle);
	odf_add_pt (state->xml, DRAW "distance", info[i].distance);

	gsf_xml_out_end_element (state->xml); /* </draw:hatch> */
}

static void
odf_write_dash_info (char const *name, gpointer data, GnmOOExport *state)
{
	GOLineDashType type = GPOINTER_TO_INT (data);
	GOLineDashSequence *lds;
	double scale;
	gboolean new = (get_gsf_odf_version () > 101);

	gsf_xml_out_start_element (state->xml, DRAW "stroke-dash");
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "name", name);
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "display-name",
					go_line_dash_as_label (type));
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "style", "rect");

	scale = new ? 1 : 0.5;

	lds = go_line_dash_get_sequence (type, scale);
	if (lds != NULL) {
		double dot_1 = lds->dash [0];
		guint n_1 = 1;
		guint i = 2;

		if (new)
			odf_add_percent (state->xml, DRAW "distance",
				    (lds->n_dash > 1) ? lds->dash[1] : 1.);
		else
			odf_add_pt (state->xml, DRAW "distance",
				    (lds->n_dash > 1) ? lds->dash[1] : 1.);

		for (; lds->n_dash > i && lds->dash[i] == dot_1; i += 2);
		gsf_xml_out_add_int (state->xml, DRAW "dots1", n_1);
		if (dot_1 == 0.)
			dot_1 = scale * 0.2;
		if (new)
			odf_add_percent (state->xml, DRAW "dots1-length", dot_1);
		else
			odf_add_pt (state->xml, DRAW "dots1-length", dot_1);
		if (lds->n_dash > i) {
			dot_1 = lds->dash [i];
			n_1 = 1;
			for (i += 2; lds->n_dash > i
				     && lds->dash[i] == dot_1; i += 2);
			gsf_xml_out_add_int (state->xml, DRAW "dots2", n_1);
			if (dot_1 == 0.)
				dot_1 = scale * 0.2;
			if (new)
				odf_add_percent (state->xml, DRAW "dots2-length",
					    dot_1);
			else
				odf_add_pt (state->xml, DRAW "dots2-length",
					    dot_1);
		}
	}

	gsf_xml_out_end_element (state->xml); /* </draw:stroke-dash> */

	go_line_dash_sequence_free (lds);
}

static void
odf_write_graph_styles (GnmOOExport *state, GsfOutput *child)
{
	int i;

	state->xml = gsf_xml_out_new (child);
	gsf_xml_out_start_element (state->xml, OFFICE "document-styles");
	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version",
					get_gsf_odf_version_string ());
	gsf_xml_out_start_element (state->xml, OFFICE "styles");

	g_hash_table_foreach (state->graph_dashes, (GHFunc) odf_write_dash_info, state);
	g_hash_table_foreach (state->graph_hatches, (GHFunc) odf_write_hatch_info, state);
	g_hash_table_foreach (state->graph_gradients, (GHFunc) odf_write_gradient_info, state);
	g_hash_table_foreach (state->graph_fill_images, (GHFunc) odf_write_fill_images_info, state);

	gsf_xml_out_end_element (state->xml); /* </office:styles> */
	gsf_xml_out_end_element (state->xml); /* </office:document-styles> */

	g_object_unref (state->xml);
	state->xml = NULL;
}

/*****************************************************************************/

static void
odf_write_gnm_settings (GnmOOExport *state)
{
	gsf_xml_out_start_element (state->xml, CONFIG "config-item-set");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", GNMSTYLE "settings");
	gsf_xml_out_start_element (state->xml, CONFIG "config-item");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", GNMSTYLE "has_foreign");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "boolean");
	odf_add_bool (state->xml, NULL, state->with_extension);

	gsf_xml_out_end_element (state->xml); /* </config:config-item> */
	gsf_xml_out_end_element (state->xml); /* </config:config-item-set> */
}

static void
odf_write_ooo_settings (GnmOOExport *state)
{
	GSList *l;

	gsf_xml_out_start_element (state->xml, CONFIG "config-item-set");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", OOO "view-settings");
	gsf_xml_out_start_element (state->xml, CONFIG "config-item-map-indexed");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "Views");
	gsf_xml_out_start_element (state->xml, CONFIG "config-item-map-entry");
	gsf_xml_out_start_element (state->xml, CONFIG "config-item");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "ViewId");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "string");
	gsf_xml_out_add_cstr (state->xml, NULL, "View1");
	gsf_xml_out_end_element (state->xml); /* </config:config-item> */
	gsf_xml_out_start_element (state->xml,
				   CONFIG "config-item-map-named");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name",
				        "Tables");

	for (l = workbook_sheets (state->wb); l != NULL; l = l->next) {
		Sheet *sheet = l->data;
		gsf_xml_out_start_element (state->xml, CONFIG "config-item-map-entry");
		gsf_xml_out_add_cstr (state->xml, CONFIG "name", sheet->name_unquoted);
		if (sheet->tab_color != NULL && !sheet->tab_color->is_auto) {
			gsf_xml_out_start_element (state->xml, CONFIG "config-item");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "TabColor");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
			gsf_xml_out_add_int (state->xml, NULL, sheet->tab_color->go_color >> 8);
			gsf_xml_out_end_element (state->xml); /* </config:config-item> */
		}
		gsf_xml_out_start_element (state->xml, CONFIG "config-item");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "ShowGrid");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "boolean");
		gsf_xml_out_add_cstr_unchecked (state->xml, NULL, "true");
		gsf_xml_out_end_element (state->xml); /* </config:config-item> */

		gsf_xml_out_end_element (state->xml); /* </config:config-item-map-entry> */
	}

	gsf_xml_out_end_element (state->xml); /* </config:config-item-map-named> */
	gsf_xml_out_end_element (state->xml); /* </config:config-item-map-entry> */
	gsf_xml_out_end_element (state->xml); /* </config:config-item-map-indexed> */
	gsf_xml_out_end_element (state->xml); /* </config:config-item-set> */
}

static void
odf_write_settings (GnmOOExport *state, GsfOutput *child)
{
	int i;

	state->xml = gsf_xml_out_new (child);
	gsf_xml_out_start_element (state->xml, OFFICE "document-settings");
	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version",
					get_gsf_odf_version_string ());

	gsf_xml_out_start_element (state->xml, OFFICE "settings");

	odf_write_gnm_settings (state);
	odf_write_ooo_settings (state);

	gsf_xml_out_end_element (state->xml); /* </office:settings> */
	gsf_xml_out_end_element (state->xml); /* </office:document-settings> */
	g_object_unref (state->xml);
	state->xml = NULL;
}

/**********************************************************************************/

static void
odf_file_entry (GsfXMLOut *out, char const *type, char const *name)
{
	gsf_xml_out_start_element (out, MANIFEST "file-entry");
	gsf_xml_out_add_cstr (out, MANIFEST "media-type", type);
	gsf_xml_out_add_cstr (out, MANIFEST "full-path", name);
	gsf_xml_out_end_element (out); /* </manifest:file-entry> */
}

static void
odf_write_graph_manifest (G_GNUC_UNUSED SheetObject *graph, char const *name, GnmOOExport *state)
{
	char *fullname = g_strdup_printf ("%s/", name);
	odf_file_entry (state->xml, "application/vnd.oasis.opendocument.chart", fullname);
	g_free(fullname);
	fullname = g_strdup_printf ("%s/content.xml", name);
	odf_file_entry (state->xml, "text/xml", fullname);
	g_free(fullname);
	fullname = g_strdup_printf ("%s/meta.xml", name);
	odf_file_entry (state->xml, "text/xml", fullname);
	g_free(fullname);
	fullname = g_strdup_printf ("%s/styles.xml", name);
	odf_file_entry (state->xml, "text/xml", fullname);
	g_free(fullname);
	fullname = g_strdup_printf ("Pictures/%s", name);
	odf_file_entry (state->xml, "image/svg+xml", fullname);
	g_free(fullname);
	fullname = g_strdup_printf ("Pictures/%s.png", name);
	odf_file_entry (state->xml, "image/png", fullname);
	g_free(fullname);
}

static void
odf_write_image_manifest (SheetObject *image, char const *name, GnmOOExport *state)
{
	char *image_type;
	char *fullname;
	char *mime;

	g_object_get (G_OBJECT (image), "image-type", &image_type, NULL);
	mime =  g_strdup_printf ("image/%s", image_type);
	fullname = g_strdup_printf ("Pictures/%s.%s", name, image_type);
	odf_file_entry (state->xml, mime, fullname);

	g_free (mime);
	g_free(fullname);
	g_free (image_type);

}

static void
odf_write_manifest (GnmOOExport *state, GsfOutput *child)
{
	GsfXMLOut *xml = gsf_xml_out_new (child);
	GSList *l;

	gsf_xml_out_set_doc_type (xml, "\n");
	gsf_xml_out_start_element (xml, MANIFEST "manifest");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:manifest",
		"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0");
	odf_file_entry (xml, "application/vnd.oasis.opendocument.spreadsheet" ,"/");
	odf_file_entry (xml, "text/xml", "content.xml");
	odf_file_entry (xml, "text/xml", "styles.xml");
	odf_file_entry (xml, "text/xml", "meta.xml");
	odf_file_entry (xml, "text/xml", "settings.xml");

	if (g_hash_table_size (state->graphs) > 0 ||
	    g_hash_table_size (state->images) > 0)
		odf_file_entry (xml, "", "Pictures/");

	state->xml = xml;
	g_hash_table_foreach (state->graphs, (GHFunc) odf_write_graph_manifest, state);
	g_hash_table_foreach (state->images, (GHFunc) odf_write_image_manifest, state);

	for (l = state->fill_image_files; l != NULL; l = l->next)
		odf_file_entry (xml, "image/png", l->data);
	go_slist_free_custom (state->fill_image_files, g_free);
	state->fill_image_files = NULL;

	state->xml = NULL;

	gsf_xml_out_end_element (xml); /* </manifest:manifest> */
	g_object_unref (xml);
}

/**********************************************************************************/
typedef enum {
	ODF_BARCOL,
	ODF_LINE,
	ODF_AREA,
	ODF_DROPBAR,
	ODF_MINMAX,
	ODF_CIRCLE,
	ODF_RADAR,
	ODF_RADARAREA,
	ODF_RING,
	ODF_SCATTER,
	ODF_SURF,
	ODF_GNM_SURF,
	ODF_XYZ_SURF,
	ODF_XYZ_GNM_SURF,
	ODF_BUBBLE,
	ODF_SCATTER_COLOUR,
	ODF_POLAR,
	ODF_GNM_BOX
} odf_chart_type_t;

static void
odf_write_label_cell_address (GnmOOExport *state, GOData const *dat)
{
	GnmExprTop const *texpr;

	if (dat == NULL)
		return;

	texpr = gnm_go_data_get_expr (dat);
	if (texpr != NULL) {
		char *str;
		GnmParsePos pp;
		parse_pos_init (&pp, WORKBOOK (state->wb), NULL, 0,0 );
		str = gnm_expr_top_as_string (texpr, &pp, state->conv);
		if (gnm_expr_top_is_rangeref (texpr))
			gsf_xml_out_add_cstr (state->xml, CHART "label-cell-address",
					      odf_strip_brackets (str));
		else if (state->with_extension)
			gsf_xml_out_add_cstr (state->xml, GNMSTYLE "label-cell-expression",
					      odf_strip_brackets (str));
		g_free (str);
	}
}

static void
odf_write_drop_line (GnmOOExport *state, GogObject const *series, char const *drop,
		     gboolean vertical)
{
	GogObjectRole const *role = gog_object_find_role_by_name (series, drop);

	if (role != NULL) {
		GSList *drops = gog_object_get_children
			(series, role);
		if (drops != NULL && drops->data != NULL) {
			char *style = odf_get_gog_style_name_from_obj (GOG_OBJECT (drops->data));

			gsf_xml_out_start_element (state->xml, GNMSTYLE "droplines");
			gsf_xml_out_add_cstr (state->xml, CHART "style-name", style);
			gsf_xml_out_end_element (state->xml); /* </gnm:droplines> */

			g_free (style);
		}
		g_slist_free (drops);
	}
}


static gboolean
odf_write_data_element (GnmOOExport *state, GOData const *data, GnmParsePos *pp,
			char const *element, char const *attribute)
{
	GnmExprTop const *texpr = gnm_go_data_get_expr (data);

	if (NULL != texpr) {
		char *str = gnm_expr_top_as_string (texpr, pp, state->conv);
		gsf_xml_out_start_element (state->xml, element);
		gsf_xml_out_add_cstr (state->xml, attribute,
				      odf_strip_brackets (str));
		g_free (str);
		return TRUE;
	}
	return FALSE;
}

static void
odf_write_data_attribute (GnmOOExport *state, GOData const *data, GnmParsePos *pp,
			  char const *attribute)
{
	GnmExprTop const *texpr = gnm_go_data_get_expr (data);

	if (NULL != texpr) {
		char *str = gnm_expr_top_as_string (texpr, pp, state->conv);
		gsf_xml_out_add_cstr (state->xml, attribute,
				      odf_strip_brackets (str));
		g_free (str);
	}
}

static gint
cmp_data_points (GObject *a, GObject *b)
{
	int ind_a = 0, ind_b = 0;

	g_object_get (a, "index", &ind_a, NULL);
	g_object_get (b, "index", &ind_b, NULL);

	if (ind_a < ind_b)
		return -1;
	else if (ind_a > ind_b)
		return 1;
	else return 0;
}

static void
odf_write_standard_series (GnmOOExport *state, GSList const *series)
{
	GnmParsePos pp;
	int i;
	parse_pos_init (&pp, WORKBOOK (state->wb), NULL, 0,0 );

	for (i = 1; NULL != series ; series = series->next, i++) {
		GOData const *dat = gog_dataset_get_dim (GOG_DATASET (series->data), GOG_MS_DIM_VALUES);
		if (NULL != dat && odf_write_data_element (state, dat, &pp, CHART "series",
							   CHART "values-cell-range-address")) {
			GogObjectRole const *role =
				gog_object_find_role_by_name
				(GOG_OBJECT (series->data), "Regression curve");
			GSList *points;
			GOData const *cat = gog_dataset_get_dim (GOG_DATASET (series->data),
								 GOG_MS_DIM_LABELS);
			char *str = odf_get_gog_style_name_from_obj (series->data);
			gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
			g_free (str);

			odf_write_label_cell_address
				(state, gog_series_get_name (GOG_SERIES (series->data)));

			if (NULL != cat && odf_write_data_element (state, cat, &pp, CHART "domain",
								   TABLE "cell-range-address"))
				gsf_xml_out_end_element (state->xml); /* </chart:domain> */

			if (role != NULL) {
				GSList *l, *regressions = gog_object_get_children
					(GOG_OBJECT (series->data), role);
				for (l = regressions; l != NULL && l->data != NULL; l = l->next) {
					GOData const *bd;
					GogObject const *regression = l->data;
					GogObject const *equation
						= gog_object_get_child_by_name (regression, "Equation");
					str = odf_get_gog_style_name_from_obj
						(GOG_OBJECT (regression));
					gsf_xml_out_start_element
						(state->xml,
						 (l == regressions) ? CHART "regression-curve"
						 : GNMSTYLE "regression-curve");
					gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);

					if (state->with_extension) {
						/* Upper and lower bounds */
						bd = gog_dataset_get_dim (GOG_DATASET (regression), 0);
						if (bd != NULL)
							odf_write_data_attribute
								(state, bd, &pp, GNMSTYLE "lower-bound");
						bd = gog_dataset_get_dim (GOG_DATASET (regression), 1);
						if (bd != NULL)
							odf_write_data_attribute
								(state, bd, &pp, GNMSTYLE "upper-bound");
					}
					if (equation != NULL) {
						GObjectClass *klass = G_OBJECT_GET_CLASS (equation);
						char const *eq_element, *eq_automatic, *eq_display, *eq_r;
						if (get_gsf_odf_version () > 101) {
							eq_element = CHART "equation";
							eq_automatic = CHART "automatic-content";
							eq_display = CHART "display-equation";
							eq_r = CHART "display-r-square";
						} else {
							eq_element = GNMSTYLE "equation";
							eq_automatic = GNMSTYLE "automatic-content";
							eq_display = GNMSTYLE "display-equation";
							eq_r = GNMSTYLE "display-r-square";
						}
						gsf_xml_out_start_element
							(state->xml, eq_element);
						odf_add_bool (state->xml, eq_automatic, TRUE);
						odf_write_plot_style_bool (state->xml, equation, klass,
									   "show-eq", eq_display);
						odf_write_plot_style_bool (state->xml, equation, klass,
									   "show-r2", eq_r);
						str = odf_get_gog_style_name_from_obj
							(GOG_OBJECT (equation));
						gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
						odf_write_gog_position (state, equation);
						gsf_xml_out_end_element (state->xml); /* </chart:equation> */
					}

					gsf_xml_out_end_element (state->xml); /* </chart:regression-curve> */
					g_free (str);
				}
			}

			/* Write data points if any */

			role = gog_object_find_role_by_name
				(GOG_OBJECT (series->data), "Point");
			if (role != NULL && NULL != (points = gog_object_get_children
						     (GOG_OBJECT (series->data), role))) {
				int index = 0, next_index = 0;
				GSList *l;
				points = g_slist_sort (points, (GCompareFunc) cmp_data_points);

				for (l = points; l != NULL; l = l->next) {
					char *style = odf_get_gog_style_name_from_obj
						(GOG_OBJECT (l->data));
					g_object_get (G_OBJECT (l->data), "index", &index, NULL);
					if (index > next_index) {
						gsf_xml_out_start_element (state->xml,
									   CHART "data-point");
						gsf_xml_out_add_int (state->xml, CHART "repeated",
								     index - next_index);
						gsf_xml_out_end_element (state->xml);
						/* CHART "data-point" */
					}
					gsf_xml_out_start_element (state->xml,
								   CHART "data-point");
					gsf_xml_out_add_cstr (state->xml, CHART "style-name", style);
					gsf_xml_out_end_element (state->xml);
					/* CHART "data-point" */
					g_free (style);
					next_index = index + 1;
				}
				g_slist_free (points);
			}

			if (state->with_extension) {
				odf_write_drop_line (state, GOG_OBJECT (series->data),
						     "Horizontal drop lines", FALSE);
				odf_write_drop_line (state, GOG_OBJECT (series->data),
						     "Vertical drop lines", TRUE);
				odf_write_drop_line (state, GOG_OBJECT (series->data),
						     "Drop lines", TRUE);
			}
			gsf_xml_out_end_element (state->xml); /* </chart:series> */
		}
	}
}

static void
odf_write_box_series (GnmOOExport *state, GSList const *series)
{
	GnmParsePos pp;
	int i;
	parse_pos_init (&pp, WORKBOOK (state->wb), NULL, 0,0 );

	for (i = 1; NULL != series ; series = series->next, i++) {
		GOData const *dat = gog_dataset_get_dim (GOG_DATASET (series->data), 0);

		if (NULL != dat) {
			GnmExprTop const *texpr = gnm_go_data_get_expr (dat);
			if (NULL != texpr) {
				char *str = gnm_expr_top_as_string (texpr, &pp, state->conv);

				gsf_xml_out_start_element (state->xml, CHART "series");
				gsf_xml_out_add_cstr (state->xml, CHART "values-cell-range-address",
						      odf_strip_brackets (str));
				g_free (str);
				str = odf_get_gog_style_name_from_obj (series->data);
				gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
				g_free (str);
				odf_write_label_cell_address
					(state, gog_series_get_name (GOG_SERIES (series->data)));
				gsf_xml_out_end_element (state->xml); /* </chart:series> */
			}
		}
	}
}

static void
odf_write_gantt_series (GnmOOExport *state, GSList const *series)
{
	GnmParsePos pp;
	int i;
	parse_pos_init (&pp, WORKBOOK (state->wb), NULL, 0,0 );

	for (i = 1; NULL != series ; series = series->next, i++) {
		GOData const *dat = gog_dataset_get_dim (GOG_DATASET (series->data), GOG_MS_DIM_VALUES);
		if (NULL != dat) {
			GnmExprTop const *texpr = gnm_go_data_get_expr (dat);
			if (NULL != texpr) {
				char *str = gnm_expr_top_as_string (texpr, &pp, state->conv);
				GOData const *cat = gog_dataset_get_dim (GOG_DATASET (series->data), GOG_MS_DIM_LABELS);
				gsf_xml_out_start_element (state->xml, CHART "series");
				gsf_xml_out_add_cstr (state->xml, CHART "values-cell-range-address",
						      odf_strip_brackets (str));
				g_free (str);
				str = odf_get_gog_style_name_from_obj (series->data);
				gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
				g_free (str);
				if (NULL != cat) {
					texpr = gnm_go_data_get_expr (cat);
					if (NULL != texpr) {
						str = gnm_expr_top_as_string (texpr, &pp, state->conv);
						gsf_xml_out_start_element (state->xml, CHART "domain");
						gsf_xml_out_add_cstr (state->xml, TABLE "cell-range-address",
								      odf_strip_brackets (str));
						gsf_xml_out_end_element (state->xml); /* </chart:domain> */
						g_free (str);
					}
				}
				gsf_xml_out_end_element (state->xml); /* </chart:series> */
			}
		}
		dat = gog_dataset_get_dim (GOG_DATASET (series->data), GOG_MS_DIM_CATEGORIES);
		if (NULL != dat) {
			GnmExprTop const *texpr = gnm_go_data_get_expr (dat);
			if (NULL != texpr) {
				char *str = gnm_expr_top_as_string (texpr, &pp, state->conv);
				gsf_xml_out_start_element (state->xml, CHART "series");
				gsf_xml_out_add_cstr (state->xml, CHART "values-cell-range-address",
						      odf_strip_brackets (str));
				g_free (str);
				str = odf_get_gog_style_name_from_obj (series->data);
				gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
				g_free (str);
				gsf_xml_out_end_element (state->xml); /* </chart:series> */
			}
		}
	}
}

static void
odf_write_bubble_series (GnmOOExport *state, GSList const *orig_series)
{
	GnmParsePos pp;
	int i, j;
	GSList const *series;
	parse_pos_init (&pp, WORKBOOK (state->wb), NULL, 0,0 );

	for (series = orig_series, i = 1; NULL != series; series = series->next, i++) {
		GOData const *dat = gog_dataset_get_dim (GOG_DATASET (series->data), 2);

		if (NULL != dat) {
			GnmExprTop const *texpr = gnm_go_data_get_expr (dat);
			if (NULL != texpr) {
				char *str = gnm_expr_top_as_string (texpr, &pp, state->conv);
				gsf_xml_out_start_element (state->xml, CHART "series");
				gsf_xml_out_add_cstr (state->xml, CHART "values-cell-range-address",
						      odf_strip_brackets (str));
				g_free (str);
				str = odf_get_gog_style_name_from_obj (series->data);
				gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
				g_free (str);
				for (j = 1; j >= 0; j--) {
					dat = gog_dataset_get_dim (GOG_DATASET (series->data), j);
					if (NULL != dat) {
						texpr = gnm_go_data_get_expr (dat);
						if (NULL != texpr) {
							str = gnm_expr_top_as_string (texpr, &pp, state->conv);
							gsf_xml_out_start_element (state->xml, CHART "domain");
							gsf_xml_out_add_cstr (state->xml, TABLE "cell-range-address",
									      odf_strip_brackets (str));
							gsf_xml_out_end_element (state->xml); /* </chart:domain> */
							g_free (str);
						}
					}
				}
			}
			gsf_xml_out_end_element (state->xml); /* </chart:series> */
		}
	}
}

static void
odf_write_min_max_series (GnmOOExport *state, GSList const *orig_series)
{
	GnmParsePos pp;
	int i, j;
	GSList const *series;
	parse_pos_init (&pp, WORKBOOK (state->wb), NULL, 0,0 );

	for (j = 1; j < 3; j++) {
		gsf_xml_out_start_element (state->xml, CHART "series");
		for (series = orig_series, i = 1; NULL != series; series = series->next, i++) {
			GOData const *dat = gog_dataset_get_dim (GOG_DATASET (series->data), j);

			if (NULL != dat) {
				GnmExprTop const *texpr = gnm_go_data_get_expr (dat);
				if (NULL != texpr) {
					char *str = gnm_expr_top_as_string (texpr, &pp, state->conv);
					gsf_xml_out_add_cstr (state->xml, CHART "values-cell-range-address",
							      odf_strip_brackets (str));
					g_free (str);
					str = odf_get_gog_style_name_from_obj (series->data);
					gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
					g_free (str);
					break;
				}
			}
		}
		gsf_xml_out_end_element (state->xml); /* </chart:series> */
	}
}


static void
odf_write_interpolation_attribute (GnmOOExport *state, GOStyle const *style, GogObject const *series)
{
	gchar *interpolation = NULL;

	g_object_get (G_OBJECT (series), "interpolation",
		      &interpolation, NULL);

	if (interpolation != NULL) {
		if (0 == strcmp (interpolation, "linear"))
			gsf_xml_out_add_cstr
				(state->xml, CHART "interpolation", "none");
		else if (0 == strcmp (interpolation, "spline"))
			gsf_xml_out_add_cstr
				(state->xml, CHART "interpolation", "b-spline");
		else if (0 == strcmp (interpolation, "cspline"))
			gsf_xml_out_add_cstr
				(state->xml, CHART "interpolation",
				 "cubic-spline");
		else {
			char *tag = g_strdup_printf ("gnm:%s", interpolation);
			gsf_xml_out_add_cstr
				(state->xml, CHART "interpolation", tag);
			g_free (tag);
		}
	}

	g_free (interpolation);
}

static void
odf_write_plot_style (GnmOOExport *state, GogObject const *plot)
{
	GObjectClass *klass = G_OBJECT_GET_CLASS (plot);
	gchar const *plot_type = G_OBJECT_TYPE_NAME (plot);
	GParamSpec *spec;

	odf_add_bool (state->xml, CHART "auto-size", TRUE);

	if (NULL != (spec = g_object_class_find_property (klass, "type"))
	    && spec->value_type == G_TYPE_STRING
	    && (G_PARAM_READABLE & spec->flags)) {
		gchar *type = NULL;
		g_object_get (G_OBJECT (plot), "type", &type, NULL);
		if (type != NULL) {
			odf_add_bool (state->xml, CHART "stacked",
				      (0== strcmp (type, "stacked")));
			odf_add_bool (state->xml, CHART "percentage",
				      (0== strcmp (type, "as_percentage")));
			g_free (type);
		}
	}

	if (NULL != (spec = g_object_class_find_property (klass, "default-separation"))
	    && spec->value_type == G_TYPE_DOUBLE
	    && (G_PARAM_READABLE & spec->flags)) {
		double default_separation = 0.;
		g_object_get (G_OBJECT (plot),
			      "default-separation", &default_separation,
			      NULL);
		if (0 == strcmp ("GogRingPlot", plot_type)) {
			if (state->with_extension)
				odf_add_percent (state->xml,
						 GNMSTYLE "default-separation",
						 default_separation);
		} else
			gsf_xml_out_add_int (state->xml,
					     CHART "pie-offset",
					     (default_separation * 100. + 0.5));
	}


	/* Note: horizontal refers to the bars and vertical to  the x-axis */
	odf_write_plot_style_bool (state->xml, plot, klass,
				   "horizontal", CHART "vertical");

	odf_write_plot_style_bool (state->xml, plot, klass,
				   "vertical", CHART "vertical");

	odf_write_plot_style_from_bool
		(state->xml, plot, klass,
		 "default-style-has-markers", CHART "symbol-type",
		 "automatic", "none");

	odf_write_plot_style_int (state->xml, plot, klass,
				  "gap-percentage", CHART "gap-width");

	odf_write_plot_style_int (state->xml, plot, klass,
				  "overlap-percentage", CHART "overlap");

	odf_write_plot_style_double_percent (state->xml, plot, klass,
					     "center-size",
					     CHART "hole-size");

	if (NULL != (spec = g_object_class_find_property (klass, "interpolation"))
	    && spec->value_type == G_TYPE_STRING
	    && (G_PARAM_READABLE & spec->flags))
		odf_write_interpolation_attribute (state, NULL, plot);

	if (0 == strcmp ( "GogXYZSurfacePlot", plot_type) ||
	    0 == strcmp ( "GogSurfacePlot", plot_type) ||
	    0 == strcmp ( "XLSurfacePlot", plot_type))
		odf_add_bool (state->xml, CHART "three-dimensional", TRUE);
	else
		odf_add_bool (state->xml, CHART "three-dimensional", FALSE);

	odf_add_bool (state->xml, CHART "lines", FALSE);

	if (state->with_extension) {
		if (0 == strcmp ( "XLSurfacePlot", plot_type))
			odf_add_bool (state->xml, GNMSTYLE "multi-series", TRUE);
		odf_write_plot_style_bool (state->xml, plot, klass,
				   "outliers", GNMSTYLE "outliers");

	odf_write_plot_style_double (state->xml, plot, klass,
				     "radius-ratio", GNMSTYLE "radius-ratio");

	odf_write_plot_style_bool (state->xml, plot, klass,
				   "vary-style-by-element", GNMSTYLE "vary-style-by-element");

	odf_write_plot_style_bool (state->xml, plot, klass,
				   "show-negatives", GNMSTYLE "show-negatives");
	}


}

static char const *
odf_get_marker (GOMarkerShape m)
{
	static struct {
		guint m;
		char const *str;
	} marks [] =
		  {{GO_MARKER_NONE, "none"},
		   {GO_MARKER_SQUARE, "square"},
		   {GO_MARKER_DIAMOND,"diamond"},
		   {GO_MARKER_TRIANGLE_DOWN,"arrow-down"},
		   {GO_MARKER_TRIANGLE_UP,"arrow-up"},
		   {GO_MARKER_TRIANGLE_RIGHT,"arrow-right"},
		   {GO_MARKER_TRIANGLE_LEFT,"arrow-left"},
		   {GO_MARKER_CIRCLE,"circle"},
		   {GO_MARKER_X,"x"},
		   {GO_MARKER_CROSS,"plus"},
		   {GO_MARKER_ASTERISK,"asterisk"},
		   {GO_MARKER_BAR,"horizontal-bar"},
		   {GO_MARKER_HALF_BAR,"vertical-bar"}, /* Not ODF */
		   {GO_MARKER_BUTTERFLY,"bow-tie"},
		   {GO_MARKER_HOURGLASS,"hourglass"},
		   {GO_MARKER_LEFT_HALF_BAR,"star"},/* Not ODF */
		   {GO_MARKER_MAX, "star"}, /* not used by us */
		   {GO_MARKER_MAX + 1, "vertical-bar"},/* not used by us */
		   {0, NULL}
		  };
	int i;
	for (i = 0; marks[i].str != NULL; i++)
		if (marks[i].m == m)
			return marks[i].str;
	return "diamond";
}

static void
odf_write_axis_style (GnmOOExport *state, GOStyle const *style, GogObject const *axis)
{
	char const *type = NULL;
	double minima = 0., maxima = 0.;
	GObjectClass *klass = G_OBJECT_GET_CLASS (axis);
	GParamSpec *spec;

	gsf_xml_out_add_cstr (state->xml, CHART "axis-position", "start");
	odf_add_bool (state->xml, CHART "display-label", TRUE);

	if (NULL != (spec = g_object_class_find_property (klass, "map-name"))
	    && spec->value_type == G_TYPE_STRING
	    && (G_PARAM_READABLE & spec->flags)) {
		g_object_get (G_OBJECT (axis), "map-name", &type, NULL);
		odf_add_bool (state->xml, CHART "logarithmic",
			      0 != strcmp (type, "Linear"));
	}
	if (gog_axis_get_bounds (GOG_AXIS (axis), &minima, &maxima)) {
		gsf_xml_out_add_float (state->xml, CHART "minimum", minima, -1);
		gsf_xml_out_add_float (state->xml, CHART "maximum", maxima, -1);
	}

	if (get_gsf_odf_version () > 101)
		odf_write_plot_style_bool
			(state->xml, axis, klass,
			 "invert-axis", CHART "reverse-direction");
	else
		odf_write_plot_style_bool
			(state->xml, axis, klass,
			 "invert-axis", GNMSTYLE "reverse-direction");
}

static void
odf_write_generic_axis_style (GnmOOExport *state, char const *style_label)
{
	odf_start_style (state->xml, style_label, "chart");
	gsf_xml_out_start_element (state->xml, STYLE "chart-properties");

	gsf_xml_out_add_cstr (state->xml, CHART "axis-position", "start");
	odf_add_bool (state->xml, CHART "display-label", TRUE);

	if (get_gsf_odf_version () > 101)
		odf_add_bool (state->xml, CHART "reverse-direction", TRUE);
	gsf_xml_out_end_element (state->xml); /* </style:chart-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */
}

static void
odf_write_circle_axes_styles (GnmOOExport *state, GogObject const *chart,
			     G_GNUC_UNUSED GogObject const *plot,
			       gchar **x_style,
			       gchar **y_style,
			       gchar **z_style)
{
	odf_write_generic_axis_style (state, "yaxis");
	*x_style = g_strdup ("yaxis");

	odf_write_generic_axis_style (state, "xaxis");
	*y_style = g_strdup ("xaxis");
}

static void
odf_write_radar_axes_styles (GnmOOExport *state, GogObject const *chart,
			     G_GNUC_UNUSED GogObject const *plot,
			       gchar **x_style,
			       gchar **y_style,
			       gchar **z_style)
{
	GogObject const *axis;

	axis = gog_object_get_child_by_name (chart, "Radial-Axis");
	if (axis != NULL)
		*y_style = odf_get_gog_style_name_from_obj (axis);

	axis = gog_object_get_child_by_name (chart, "Circular-Axis");
	if (axis != NULL)
		*x_style = odf_get_gog_style_name_from_obj (axis);
}

static void
odf_write_standard_axes_styles (GnmOOExport *state, GogObject const *chart,
				GogObject const *plot,
				gchar **x_style,
				gchar **y_style,
				gchar **z_style)
{
	GogObject const *axis;

	axis = gog_object_get_child_by_name (chart, "X-Axis");
	if (axis != NULL)
		*x_style = odf_get_gog_style_name_from_obj (axis);

	axis = gog_object_get_child_by_name (chart, "Y-Axis");
	if (axis != NULL)
		*y_style = odf_get_gog_style_name_from_obj (axis);
}

static void
odf_write_surface_axes_styles (GnmOOExport *state, GogObject const *chart,
			       GogObject const *plot,
				gchar **x_style,
				gchar **y_style,
				gchar **z_style)
{
	GogObject const *axis;

	odf_write_standard_axes_styles (state, chart, plot, x_style, y_style, z_style);

	axis = gog_object_get_child_by_name (chart, "Z-Axis");
	if (axis != NULL)
		*z_style = odf_get_gog_style_name_from_obj (axis);
}

static void
odf_write_one_axis_grid (GnmOOExport *state, GogObject const *axis,
			 char const *role, char const *class)
{
	GogObject const *grid;

	grid = gog_object_get_child_by_name (axis, role);
	if (grid) {
		gsf_xml_out_start_element (state->xml, CHART "grid");
		gsf_xml_out_add_cstr (state->xml, CHART "class", class);
		gsf_xml_out_end_element (state->xml); /* </chart:grid> */
	}
}

static void
odf_write_axis_grid (GnmOOExport *state, GogObject const *axis)
{
	g_return_if_fail (axis != NULL);
	odf_write_one_axis_grid (state, axis, "MajorGrid", "major");
	odf_write_one_axis_grid (state, axis, "MinorGrid", "minor");
}

static void
odf_write_title (GnmOOExport *state, GogObject const *title,
		 char const *id, gboolean allow_content)
{
	if (title != NULL && id != NULL) {
		GOData const *dat = gog_dataset_get_dim (GOG_DATASET(title),0);
		if (dat != NULL) {
			GnmExprTop const *texpr = gnm_go_data_get_expr (dat);
			if (texpr != NULL) {
				GnmParsePos pp;
				char *formula;
				char *name;

				gsf_xml_out_start_element (state->xml, id);

				name = odf_get_gog_style_name_from_obj (title);

				if (name != NULL) {
					gsf_xml_out_add_cstr (state->xml, CHART "style-name",
								      name);
					g_free (name);
				}

				parse_pos_init (&pp, WORKBOOK (state->wb), NULL, 0,0 );
				formula = gnm_expr_top_as_string (texpr, &pp, state->conv);

				if (gnm_expr_top_is_rangeref (texpr)) {
					char *f = odf_strip_brackets (formula);
					gsf_xml_out_add_cstr (state->xml,
							      TABLE "cell-address", f);
					gsf_xml_out_add_cstr (state->xml,
							      TABLE "cell-range", f);
				} else if (GNM_EXPR_GET_OPER (texpr->expr)
					   == GNM_EXPR_OP_CONSTANT
					   && texpr->expr->constant.value->type == VALUE_STRING
					   && allow_content) {
					gboolean white_written = TRUE;
					char const *str;
					gsf_xml_out_start_element (state->xml, TEXT "p");
					str = value_peek_string (texpr->expr->constant.value);
					odf_add_chars (state, str, strlen (str),
						       &white_written);
					gsf_xml_out_end_element (state->xml); /* </text:p> */
				} else {
					gboolean white_written = TRUE;
					if (state->with_extension)
						gsf_xml_out_add_cstr (state->xml,
								      GNMSTYLE "expression",
								      formula);
					if (allow_content) {
						gsf_xml_out_start_element
							(state->xml, TEXT "p");
						odf_add_chars (state, formula,
							       strlen (formula),
							       &white_written);
						gsf_xml_out_end_element (state->xml);
						/* </text:p> */
					}
				}
				gsf_xml_out_end_element (state->xml); /* </chart:title> */
				g_free (formula);
			}
		}
	}
}

static void
odf_write_label (GnmOOExport *state, GogObject const *axis)
{
	GSList *labels = gog_object_get_children
		(axis, gog_object_find_role_by_name (axis, "Label"));

	if (labels != NULL) {
		GogObject const *label = NULL;

		label = labels->data;
		odf_write_title (state, label, CHART "title", TRUE);
		g_slist_free (labels);
	}


}

static gboolean
odf_match_gradient (GOStyle const *old, GOStyle const *new)
{
	gboolean result;

	if (old->fill.gradient.brightness != new->fill.gradient.brightness)
		return FALSE;

	if (old->fill.gradient.brightness >= 0.)
		result = (old->fill.gradient.brightness == new->fill.gradient.brightness);
	else
		result = (old->fill.pattern.fore == new->fill.pattern.fore);

	return (result && (old->fill.gradient.dir == new->fill.gradient.dir) &&
		(old->fill.pattern.back == new->fill.pattern.back));
}

static gchar *
odf_get_gradient_name (GnmOOExport *state, GOStyle const* style)
{
	gchar const *grad = g_hash_table_lookup (state->graph_gradients,
						(gpointer) style);
	gchar *new_name;
	if (grad != NULL)
		return g_strdup (grad);

	new_name =  g_strdup_printf ("Gradient-%i", g_hash_table_size (state->graph_gradients));
	g_hash_table_insert (state->graph_gradients,
			     (gpointer) style, g_strdup (new_name));
	return new_name;
}

static gboolean
odf_match_image (GOImage *old, GOImage *new)
{
	return go_image_same_pixbuf (old, new);
}


static gchar *
odf_get_image_name (GnmOOExport *state, GOStyle const* style)
{
	gchar const *image = g_hash_table_lookup (state->graph_fill_images,
						  (gpointer) style->fill.image.image);
	gchar *new_name;
	if (image != NULL)
		return g_strdup (image);

	new_name =  g_strdup_printf ("Fill-Image-%i",
				     g_hash_table_size (state->graph_fill_images));
	g_hash_table_insert (state->graph_fill_images,
			     (gpointer) style->fill.image.image, g_strdup (new_name));
	return new_name;
}

static gboolean
odf_match_pattern (GOPattern const *old, GOPattern const *new)
{
	return (old->pattern == new->pattern &&
		old->back == new->back &&
		old->fore == new->fore);
}

static gchar *
odf_get_pattern_name (GnmOOExport *state, GOStyle const* style)
{
	gchar const *hatch = g_hash_table_lookup (state->graph_hatches,
						  (gpointer) &style->fill.pattern);
	gchar *new_name;
	if (hatch != NULL)
		return g_strdup (hatch);

	new_name =  g_strdup_printf ("Pattern-%i-%i", style->fill.pattern.pattern,
				     g_hash_table_size (state->graph_hatches));
	g_hash_table_insert (state->graph_hatches,
			     (gpointer) &style->fill.pattern, g_strdup (new_name));
	return new_name;
}

static void
odf_write_gog_style_graphic (GnmOOExport *state, GOStyle const *style)
{
	char const *image_types[] =
		{"stretch", "repeat", "no-repeat"};
	if (style != NULL) {
		char *color = NULL;

		switch (style->fill.type) {
		case GO_STYLE_FILL_NONE:
			gsf_xml_out_add_cstr (state->xml, DRAW "fill", "none");
			break;
		case GO_STYLE_FILL_PATTERN:
			if (style->fill.pattern.pattern == GO_PATTERN_SOLID) {
				gsf_xml_out_add_cstr (state->xml, DRAW "fill", "solid");
				if (!style->fill.auto_back) {
					color = odf_go_color_to_string (style->fill.pattern.back);
					gsf_xml_out_add_cstr (state->xml, DRAW "fill-color", color);
				}
			} else if (style->fill.pattern.pattern == GO_PATTERN_FOREGROUND_SOLID) {
				gsf_xml_out_add_cstr (state->xml, DRAW "fill", "solid");
				if (!style->fill.auto_fore) {
					color = odf_go_color_to_string (style->fill.pattern.fore);
					gsf_xml_out_add_cstr (state->xml, DRAW "fill-color", color);
				}
			} else {
				gchar *hatch = odf_get_pattern_name (state, style);
				gsf_xml_out_add_cstr (state->xml, DRAW "fill", "hatch");
				gsf_xml_out_add_cstr (state->xml, DRAW "fill-hatch-name",
						      hatch);
				if (!style->fill.auto_back) {
					color = odf_go_color_to_string (style->fill.pattern.back);
					gsf_xml_out_add_cstr (state->xml, DRAW "fill-color", color);
				}
				g_free (hatch);
				odf_add_bool (state->xml, DRAW "fill-hatch-solid", TRUE);
				if (state->with_extension)
					gsf_xml_out_add_int
						(state->xml,
						 GNMSTYLE "pattern",
						 style->fill.pattern.pattern);
			}
			g_free (color);
			break;
		case GO_STYLE_FILL_GRADIENT: {
			gchar *grad = odf_get_gradient_name (state, style);
			gsf_xml_out_add_cstr (state->xml, DRAW "fill", "gradient");
			gsf_xml_out_add_cstr (state->xml, DRAW "fill-gradient-name", grad);
			g_free (grad);
			break;
		}
		case GO_STYLE_FILL_IMAGE: {
			gchar *image = odf_get_image_name (state, style);
			gsf_xml_out_add_cstr (state->xml, DRAW "fill", "bitmap");
			gsf_xml_out_add_cstr (state->xml, DRAW "fill-image-name", image);
			g_free (image);
			if (0 <= style->fill.image.type &&
			    style->fill.image.type < (int)G_N_ELEMENTS (image_types))
				gsf_xml_out_add_cstr (state->xml, STYLE "repeat",
						      image_types [style->fill.image.type]);
			else g_warning ("Unexpected GOImageType value");
			break;
		}
		}
		if (go_style_is_line_visible (style)) {
			GOLineDashType dash_type = style->line.dash_type;

			if (dash_type == GO_LINE_SOLID)
				gsf_xml_out_add_cstr (state->xml,
						      DRAW "stroke", "solid");
			else {
				char const *dash = go_line_dash_as_str (dash_type);
				gsf_xml_out_add_cstr (state->xml,
						      DRAW "stroke", "dash");
				gsf_xml_out_add_cstr
					(state->xml,
					 DRAW "stroke-dash", dash);
				g_hash_table_insert (state->graph_dashes, g_strdup (dash),
						     GINT_TO_POINTER (dash_type));
			}
			if (style->line.width == 0.0)
				odf_add_pt (state->xml, SVG "stroke-width", 1.);
			else if (style->line.width > 0.0)
				odf_add_pt (state->xml, SVG "stroke-width",
					    style->line.width);
			if (!style->line.auto_color) {
				color = odf_go_color_to_string (style->line.color);
				gsf_xml_out_add_cstr (state->xml, SVG "stroke-color",
						      color);

			}
		} else {
			gsf_xml_out_add_cstr (state->xml, DRAW "stroke", "none");
		}
	}
}

static void
odf_write_gog_style_text (GnmOOExport *state, GOStyle const *style)
{
	if (style != NULL) {
		PangoFontDescription const *desc = style->font.font->desc;
		PangoFontMask mask = pango_font_description_get_set_fields (desc);
		int val = style->text_layout.angle;

		odf_add_angle (state->xml, STYLE "text-rotation-angle", val);

		if (mask & PANGO_FONT_MASK_SIZE)
			odf_add_pt (state->xml, FOSTYLE "font-size",
				    pango_font_description_get_size
				    (style->font.font->desc)
				    / (double)PANGO_SCALE);

		if (mask & PANGO_FONT_MASK_VARIANT) {
			PangoVariant var = pango_font_description_get_variant (desc);
			switch (var) {
			case PANGO_VARIANT_NORMAL:
				gsf_xml_out_add_cstr (state->xml,
						      FOSTYLE "font-variant", "normal");
				break;
			case PANGO_VARIANT_SMALL_CAPS:
				gsf_xml_out_add_cstr (state->xml,
						      FOSTYLE "font-variant",
						      "small-caps");
				break;
			default:
				break;
			}
		}
		/*Note that we should be using style:font-name instead of fo:font-family*/
		if (mask & PANGO_FONT_MASK_FAMILY)
			gsf_xml_out_add_cstr
				(state->xml,
				 FOSTYLE "font-family",
				 pango_font_description_get_family (desc));
		if (mask & PANGO_FONT_MASK_STYLE) {
			PangoStyle s = pango_font_description_get_style (desc);
			switch (s) {
			case PANGO_STYLE_NORMAL:
				gsf_xml_out_add_cstr (state->xml,
						      FOSTYLE "font-style", "normal");
				break;
			case PANGO_STYLE_OBLIQUE:
				gsf_xml_out_add_cstr (state->xml,
						      FOSTYLE "font-style", "oblique");
				break;
			case PANGO_STYLE_ITALIC:
				gsf_xml_out_add_cstr (state->xml,
						      FOSTYLE "font-style", "italic");
				break;
			default:
				break;
			}
		}
		if (mask & PANGO_FONT_MASK_WEIGHT) {
			PangoWeight w = pango_font_description_get_weight (desc);
			if (w > 900)
				w = 900;
			gsf_xml_out_add_int (state->xml, FOSTYLE "font-weight", w);
		}

		if ((mask & PANGO_FONT_MASK_STRETCH) && state->with_extension)
			gsf_xml_out_add_int (state->xml, GNMSTYLE "font-stretch-pango",
					     pango_font_description_get_stretch (desc));
		if ((mask & PANGO_FONT_MASK_GRAVITY) && state->with_extension)
			gsf_xml_out_add_int (state->xml, GNMSTYLE "font-gravity-pango",
					     pango_font_description_get_gravity (desc));
	}
}

static void
odf_write_gog_style_chart (GnmOOExport *state, GOStyle const *style, GogObject const *obj)
{
	gchar const *type = G_OBJECT_TYPE_NAME (G_OBJECT (obj));
	GObjectClass *klass = G_OBJECT_GET_CLASS (G_OBJECT (obj));
	void (*func) (GnmOOExport *state, GOStyle const *style, GogObject const *obj);
	GParamSpec *spec;

	if (GOG_IS_PLOT (obj))
		odf_write_plot_style (state, obj);

	func = g_hash_table_lookup (state->chart_props_hash, type);
	if (func != NULL)
		func (state, style, obj);

	if (style != NULL) {
		if (go_style_is_line_visible (style)) {
			odf_add_bool (state->xml, CHART "lines", TRUE);
		} else {
			odf_add_bool (state->xml, CHART "lines", FALSE);
		}

		if (style->marker.auto_shape) {
			if (NULL != (spec = g_object_class_find_property (klass, "type"))
			    && spec->value_type == G_TYPE_BOOLEAN
			    && (G_PARAM_READABLE & spec->flags)) {
				gboolean has_marker = TRUE;
				g_object_get (G_OBJECT (obj), "default-style-has-markers",
					      &has_marker, NULL);
				if (has_marker)
					gsf_xml_out_add_cstr (state->xml, CHART "symbol-type",
						      "automatic");
				else
					gsf_xml_out_add_cstr (state->xml, CHART "symbol-type",
							      "none");
			}
		} else {
			GOMarkerShape m
				= go_marker_get_shape (go_style_get_marker ((GOStyle *)style));
			if (m == GO_MARKER_NONE)
				gsf_xml_out_add_cstr (state->xml, CHART "symbol-type",
						      "none");
			else {
				gsf_xml_out_add_cstr (state->xml, CHART "symbol-type",
						      "named-symbol");
				gsf_xml_out_add_cstr
					(state->xml, CHART "symbol-name", odf_get_marker (m));
			}
		}
	}
}

static void
odf_write_gog_style (GnmOOExport *state, GOStyle const *style,
		     GogObject const *obj)
{
	char *name = odf_get_gog_style_name (style, obj);
	if (name != NULL) {
		odf_start_style (state->xml, name, "chart");

		gsf_xml_out_start_element (state->xml, STYLE "chart-properties");
		odf_write_gog_style_chart (state, style, obj);
		gsf_xml_out_end_element (state->xml); /* </style:chart-properties> */

		gsf_xml_out_start_element (state->xml, STYLE "graphic-properties");
		odf_write_gog_style_graphic (state, style);
		gsf_xml_out_end_element (state->xml); /* </style:graphic-properties> */

		gsf_xml_out_start_element (state->xml, STYLE "paragraph-properties");
		gsf_xml_out_end_element (state->xml); /* </style:paragraph-properties> */

		gsf_xml_out_start_element (state->xml, STYLE "text-properties");
		odf_write_gog_style_text (state, style);
		gsf_xml_out_end_element (state->xml); /* </style:text-properties> */

		gsf_xml_out_end_element (state->xml); /* </style:style> */

		g_free (name);
	}
}

static void
odf_write_gog_styles (GogObject const *obj, GnmOOExport *state)
{
	GObjectClass *klass = G_OBJECT_GET_CLASS (G_OBJECT (obj));
	GSList *children;

	if (NULL != g_object_class_find_property (klass, "style")) {
		GOStyle const *style = NULL;
		g_object_get (G_OBJECT (obj), "style", &style, NULL);
		odf_write_gog_style (state, style, obj);
		if (style != NULL) {
			g_object_unref (G_OBJECT (style));
		}
	} else
		odf_write_gog_style (state, NULL, obj);

	children = gog_object_get_children (obj, NULL);
	g_slist_foreach (children, (GFunc) odf_write_gog_styles, state);
	g_slist_free (children);
}

static void
odf_write_axis_categories (GnmOOExport *state, GSList const *series)
{
	if (series != NULL && series->data != NULL) {
		GOData const *cat = gog_dataset_get_dim (GOG_DATASET (series->data), GOG_MS_DIM_LABELS);
		if (NULL != cat) {
			GnmExprTop const *texpr = gnm_go_data_get_expr (cat);
			if (NULL != texpr) {
				char *cra;
				GnmParsePos pp;
				parse_pos_init (&pp, WORKBOOK (state->wb), NULL, 0,0 );
				cra = gnm_expr_top_as_string (texpr, &pp, state->conv);

				gsf_xml_out_start_element (state->xml, CHART "categories");
				gsf_xml_out_add_cstr (state->xml, TABLE "cell-range-address",
						      odf_strip_brackets (cra));
				gsf_xml_out_end_element (state->xml); /* </chart:categories> */

				g_free (cra);
			}
		}
	}
}

static void
odf_write_axis (GnmOOExport *state, GogObject const *chart, char const *axis_role,
		char const *style_label,
		char const *dimension, odf_chart_type_t gtype, GSList const *series)
{
	GogObject const *axis;

	if (axis_role == NULL)
		return;

	axis = gog_object_get_child_by_name (chart, axis_role);
	if (axis != NULL) {
		gsf_xml_out_start_element (state->xml, CHART "axis");
		gsf_xml_out_add_cstr (state->xml, CHART "dimension", dimension);
		gsf_xml_out_add_cstr (state->xml, CHART "style-name", style_label);
		odf_write_label (state, axis);
		odf_write_axis_grid (state, axis);
		odf_write_axis_categories (state, series);
		gsf_xml_out_end_element (state->xml); /* </chart:axis> */
	}
}

static void
odf_write_generic_axis (GnmOOExport *state, GogObject const *chart,
			char const *axis_role,
			char const *style_label,
			char const *dimension, odf_chart_type_t gtype,
			GSList const *series)
{
	gsf_xml_out_start_element (state->xml, CHART "axis");
	gsf_xml_out_add_cstr (state->xml, CHART "dimension", dimension);
	gsf_xml_out_add_cstr (state->xml, CHART "style-name", style_label);
	odf_write_axis_categories (state, series);
	gsf_xml_out_end_element (state->xml); /* </chart:axis> */
}

static void
odf_write_plot (GnmOOExport *state, SheetObject *so, GogObject const *chart, GogObject const *plot)
{
	char const *plot_type = G_OBJECT_TYPE_NAME (plot);
	SheetObjectAnchor const *anchor = sheet_object_get_anchor (so);
	double res_pts[4] = {0.,0.,0.,0.};
	GSList const *series, *l;
	GogObject const *wall = gog_object_get_child_by_name (chart, "Backplane");
	GogObject const *legend = gog_object_get_child_by_name (chart, "Legend");
	GSList *titles = gog_object_get_children (chart, gog_object_find_role_by_name (chart, "Title"));
	char *name;
	gchar *x_style = NULL;
	gchar *y_style = NULL;
	gchar *z_style = NULL;

	static struct {
		char const * type;
		char const *odf_plot_type;
		odf_chart_type_t gtype;
		double pad;
		char const * x_axis_name;
		char const * y_axis_name;
		char const * z_axis_name;
		void (*odf_write_axes_styles)  (GnmOOExport *state,
					        GogObject const *chart,
						GogObject const *plot,
						gchar **x_style,
						gchar **y_style,
						gchar **z_style);
		void (*odf_write_series)       (GnmOOExport *state,
						GSList const *series);
		void (*odf_write_x_axis) (GnmOOExport *state,
					  GogObject const *chart,
					  char const *axis_role,
					  char const *style_label,
					  char const *dimension,
					  odf_chart_type_t gtype,
					  GSList const *series);
		void (*odf_write_y_axis) (GnmOOExport *state,
					  GogObject const *chart,
					  char const *axis_role,
					  char const *style_label,
					  char const *dimension,
					  odf_chart_type_t gtype,
					  GSList const *series);
		void (*odf_write_z_axis) (GnmOOExport *state,
					  GogObject const *chart,
					  char const *axis_role,
					  char const *style_label,
					  char const *dimension,
					  odf_chart_type_t gtype,
					  GSList const *series);
	} *this_plot, plots[] = {
		{ "GogBarColPlot", CHART "bar", ODF_BARCOL,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogLinePlot", CHART "line", ODF_LINE,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogPolarPlot", GNMSTYLE "polar", ODF_POLAR,
		  20., "Circular-Axis", "Radial-Axis", NULL,
		  odf_write_radar_axes_styles,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogAreaPlot", CHART "area", ODF_AREA,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogDropBarPlot", CHART "gantt", ODF_DROPBAR,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_gantt_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogMinMaxPlot", CHART "stock", ODF_MINMAX,
		  10., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_min_max_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogPiePlot", CHART "circle", ODF_CIRCLE,
		  5., "X-Axis", "Y-Axis", NULL, odf_write_circle_axes_styles,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogRadarPlot", CHART "radar", ODF_RADAR,
		  10., "Circular-Axis", "Radial-Axis", NULL,
		  odf_write_radar_axes_styles,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogRadarAreaPlot", CHART "filled-radar", ODF_RADARAREA,
		  10., "X-Axis", "Y-Axis", NULL, odf_write_radar_axes_styles,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogRingPlot", CHART "ring", ODF_RING,
		  10., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_standard_series,
		  odf_write_generic_axis, odf_write_generic_axis, NULL},
		{ "GogXYPlot", CHART "scatter", ODF_SCATTER,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogContourPlot", CHART "surface", ODF_SURF,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_bubble_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogXYZContourPlot", GNMSTYLE "xyz-surface", ODF_XYZ_SURF,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_bubble_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogXYZSurfacePlot", GNMSTYLE "xyz-surface", ODF_XYZ_GNM_SURF,
		  20., "X-Axis", "Y-Axis", "Z-Axis", odf_write_surface_axes_styles,
		  odf_write_bubble_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogSurfacePlot", CHART "surface", ODF_GNM_SURF,
		  20., "X-Axis", "Y-Axis", "Z-Axis", odf_write_surface_axes_styles,
		  odf_write_bubble_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogBubblePlot", CHART "bubble", ODF_BUBBLE,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_bubble_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogXYColorPlot", GNMSTYLE "scatter-color", ODF_SCATTER_COLOUR,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_bubble_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "XLSurfacePlot", CHART "surface", ODF_GNM_SURF,
		  20., "X-Axis", "Y-Axis", "Z-Axis", odf_write_surface_axes_styles,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogBoxPlot", GNMSTYLE "box", ODF_GNM_BOX,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_box_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ NULL, NULL, 0,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis}
	};

	for (this_plot = &plots[0]; this_plot->type != NULL; this_plot++)
		if (0 == strcmp (plot_type, this_plot->type))
			break;

	if (this_plot->type == NULL) {
		g_print ("Encountered unknown chart type %s\n", plot_type);
		this_plot = &plots[0];
	}

	series = gog_plot_get_series (GOG_PLOT (plot));

	gsf_xml_out_start_element (state->xml, OFFICE "automatic-styles");

	if (this_plot->odf_write_axes_styles != NULL)
		this_plot->odf_write_axes_styles (state, chart, plot,
						  &x_style, &y_style, &z_style);

	odf_start_style (state->xml, "plotstyle", "chart");
	gsf_xml_out_start_element (state->xml, STYLE "chart-properties");
	odf_add_bool (state->xml, CHART "auto-size", TRUE);
	gsf_xml_out_end_element (state->xml); /* </style:chart-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_write_gog_styles (chart, state);

	gsf_xml_out_end_element (state->xml); /* </office:automatic-styles> */

	gsf_xml_out_start_element (state->xml, OFFICE "body");
	gsf_xml_out_start_element (state->xml, OFFICE "chart");
	gsf_xml_out_start_element (state->xml, CHART "chart");

       	sheet_object_anchor_to_pts (anchor, state->sheet, res_pts);
	odf_add_pt (state->xml, SVG "width", res_pts[2] - res_pts[0] - 2 * this_plot->pad);
	odf_add_pt (state->xml, SVG "height", res_pts[3] - res_pts[1] - 2 * this_plot->pad);

	if (get_gsf_odf_version () > 101)
		gsf_xml_out_add_cstr (state->xml, XLINK "href", "..");
	gsf_xml_out_add_cstr (state->xml, CHART "class", this_plot->odf_plot_type);
	gsf_xml_out_add_cstr (state->xml, CHART "style-name", "plotstyle");

	/* Set up title */

	if (titles != NULL) {
		GogObject const *title = titles->data;
		odf_write_title (state, title, CHART "title", TRUE);
		if (titles->next != NULL) {
			title = titles->next->data;
			odf_write_title (state, title, CHART "subtitle", TRUE);
		}

		g_slist_free (titles);
	}


	/* Set up legend if appropriate*/

	if (legend != NULL) {
		GogObjectPosition flags;
		char *style_name = odf_get_gog_style_name_from_obj
			(legend);
		GSList *ltitles = gog_object_get_children
			(legend, gog_object_find_role_by_name
			 (legend, "Title"));

		flags = gog_object_get_position_flags
			(legend, GOG_POSITION_COMPASS);

		gsf_xml_out_start_element (state->xml, CHART "legend");
		gsf_xml_out_add_cstr (state->xml,
					      CHART "style-name",
					      style_name);
		g_free (style_name);

		if (flags) {
			GString *compass = g_string_new (NULL);

			if (flags & GOG_POSITION_N)
				g_string_append (compass, "top");
			if (flags & GOG_POSITION_S)
				g_string_append (compass, "bottom");
			if ((flags & (GOG_POSITION_S | GOG_POSITION_N)) &&
			    (flags & (GOG_POSITION_E | GOG_POSITION_W)))
				g_string_append (compass, "-");
			if (flags & GOG_POSITION_E)
				g_string_append (compass, "end");
			if (flags & GOG_POSITION_W)
				g_string_append (compass, "start");

			gsf_xml_out_add_cstr (state->xml,
					      CHART "legend-position",
					      compass->str);

			g_string_free (compass, TRUE);
		}

		if (ltitles != NULL) {
			GogObject const *title = ltitles->data;

			if (state->with_extension)
				odf_write_title (state, title,
						 GNMSTYLE "title", get_gsf_odf_version () > 101);
			else if (get_gsf_odf_version () > 101) {
				GOData const *dat =
					gog_dataset_get_dim (GOG_DATASET(title),0);

				if (dat != NULL) {
					GnmExprTop const *texpr
						= gnm_go_data_get_expr (dat);
					if (texpr != NULL &&
					    GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_CONSTANT
					    && texpr->expr->constant.value->type == VALUE_STRING) {
						gboolean white_written = TRUE;
						char const *str;
						gsf_xml_out_start_element (state->xml, TEXT "p");
						str = value_peek_string (texpr->expr->constant.value);
						odf_add_chars (state, str, strlen (str),
							       &white_written);
						gsf_xml_out_end_element (state->xml); /* </text:p> */
					}
				}

			}
			g_slist_free (ltitles);
		}

		gsf_xml_out_end_element (state->xml); /* </chart:legend> */
	}

	gsf_xml_out_start_element (state->xml, CHART "plot-area");

	name = odf_get_gog_style_name_from_obj (plot);
	if (name != NULL) {
		gsf_xml_out_add_cstr (state->xml, CHART "style-name", name);
		g_free (name);
	}

	if (get_gsf_odf_version () <= 101) {
		for ( l = series; NULL != l ; l = l->next) {
			GOData const *dat = gog_dataset_get_dim
				(GOG_DATASET (l->data), GOG_MS_DIM_VALUES);
			if (NULL != dat) {
				GnmExprTop const *texpr = gnm_go_data_get_expr (dat);
				if (NULL != texpr) {
					GnmParsePos pp;
					char *str;
					parse_pos_init (&pp, WORKBOOK (state->wb), NULL, 0,0 );
					str = gnm_expr_top_as_string (texpr, &pp, state->conv);
					gsf_xml_out_add_cstr (state->xml, TABLE "cell-range-address",
							      odf_strip_brackets (str));
					g_free (str);
					break;
				}
			}
		}
	}

	odf_write_gog_plot_area_position (state, chart);

	if (this_plot->odf_write_z_axis)
		this_plot->odf_write_z_axis
			(state, chart, this_plot->z_axis_name, z_style, "z",
			 this_plot->gtype, series);
	if (this_plot->odf_write_y_axis)
		this_plot->odf_write_y_axis
			(state, chart, this_plot->y_axis_name, y_style, "y",
			 this_plot->gtype, series);
	if (this_plot->odf_write_x_axis)
		this_plot->odf_write_x_axis
			(state, chart, this_plot->x_axis_name, x_style, "x",
			 this_plot->gtype, series);

	if (this_plot->odf_write_series != NULL)
		this_plot->odf_write_series (state, series);

	if (wall != NULL) {
		char *name = odf_get_gog_style_name_from_obj (wall);

		gsf_xml_out_start_element (state->xml, CHART "wall");
		odf_add_pt (state->xml, SVG "width", res_pts[2] - res_pts[0] - 2 * this_plot->pad);
		if (name != NULL)
			gsf_xml_out_add_cstr (state->xml, CHART "style-name", name);
		gsf_xml_out_end_element (state->xml); /* </chart:wall> */

		g_free (name);
	}
	gsf_xml_out_end_element (state->xml); /* </chart:plot_area> */
	gsf_xml_out_end_element (state->xml); /* </chart:chart> */
	gsf_xml_out_end_element (state->xml); /* </office:chart> */
	gsf_xml_out_end_element (state->xml); /* </office:body> */

	g_free (x_style);
	g_free (y_style);
	g_free (z_style);
}


static void
odf_write_graph_content (GnmOOExport *state, GsfOutput *child, SheetObject *so)
{
	int i;
	GogGraph const	*graph;
	gboolean plot_written = FALSE;

	state->xml = gsf_xml_out_new (child);
	gsf_xml_out_set_doc_type (state->xml, "\n");
	gsf_xml_out_start_element (state->xml, OFFICE "document-content");

	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version",
					get_gsf_odf_version_string ());

	graph = sheet_object_graph_get_gog (so);
	if (graph != NULL) {
		GogObjectRole const *role =
			gog_object_find_role_by_name (GOG_OBJECT (graph), "Chart");
		if (role != NULL) {
			GSList *charts = gog_object_get_children
				(GOG_OBJECT (graph), role);

			if (charts != NULL && charts->data != NULL) {
				GogObject const	*chart = charts->data;
				role = gog_object_find_role_by_name (chart, "Plot");
				if (role != NULL) {
					GSList *plots = gog_object_get_children
						(chart, gog_object_find_role_by_name (chart, "Plot"));
					if (plots != NULL && plots->data != NULL) {
						odf_write_plot (state, so, chart, plots->data);
						plot_written = TRUE;
					}
					g_slist_free (plots);
				}
			}
			g_slist_free (charts);
		}
	}
	if (!plot_written) {
		gsf_xml_out_start_element (state->xml, OFFICE "body");
		gsf_xml_out_start_element (state->xml, OFFICE "chart");
		gsf_xml_out_start_element (state->xml, CHART "chart");
		gsf_xml_out_add_cstr (state->xml, CHART "class", GNMSTYLE "none");
		gsf_xml_out_start_element (state->xml, CHART "plot-area");
		gsf_xml_out_end_element (state->xml); /* </chart:plotarea> */
		gsf_xml_out_end_element (state->xml); /* </chart:chart> */
		gsf_xml_out_end_element (state->xml); /* </office:chart> */
		gsf_xml_out_end_element (state->xml); /* </office:body> */
	}
	gsf_xml_out_end_element (state->xml); /* </office:document-content> */
	g_object_unref (state->xml);
	state->xml = NULL;
}

/**********************************************************************************/

static void
odf_write_images (SheetObjectImage *image, char const *name, GnmOOExport *state)
{
	char *image_type;
	char *fullname;
	GsfOutput  *child;
	GByteArray *bytes;

	g_object_get (G_OBJECT (image),
		      "image-type", &image_type,
		      "image-data", &bytes,
		      NULL);
	fullname = g_strdup_printf ("Pictures/%s.%s", name, image_type);

	child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
							"compression-level", GSF_ZIP_DEFLATED,
							NULL);
	if (NULL != child) {
		gsf_output_write (child, bytes->len, bytes->data);
		gsf_output_close (child);
		g_object_unref (G_OBJECT (child));
	}

	g_free(fullname);
	g_free (image_type);

	odf_update_progress (state, state->graph_progress);
}

static void
odf_write_drop (GnmOOExport *state, GOStyle const *style, GogObject const *obj)
{
	GogObjectRole const *h_role = gog_object_find_role_by_name
		(obj->parent, "Horizontal drop lines");
	gboolean vertical = !(h_role == obj->role);

	odf_add_bool (state->xml, CHART "vertical", vertical);
}

static void
odf_write_lin_reg (GnmOOExport *state, GOStyle const *style, GogObject const *obj)
{
	gsf_xml_out_add_cstr (state->xml, CHART "regression-type",  "linear");
	if (state->with_extension) {
		GObjectClass *klass = G_OBJECT_GET_CLASS (G_OBJECT (obj));
		odf_write_plot_style_bool (state->xml, obj, klass,
					  "affine", GNMSTYLE "regression-affine");
		odf_write_plot_style_uint (state->xml, obj, klass,
					  "dims", GNMSTYLE "regression-polynomial-dims");
	}
}

static void
odf_write_polynom_reg (GnmOOExport *state, GOStyle const *style, GogObject const *obj)
{
	if (state->with_extension) {
		GObjectClass *klass = G_OBJECT_GET_CLASS (G_OBJECT (obj));

		gsf_xml_out_add_cstr (state->xml, CHART "regression-type",
				      GNMSTYLE "polynomial");
		odf_write_plot_style_uint (state->xml, obj, klass,
					  "dims", GNMSTYLE "regression-polynomial-dims");
		odf_write_plot_style_bool (state->xml, obj, klass,
					  "affine", GNMSTYLE "regression-affine");
	}
}

static void
odf_write_exp_reg (GnmOOExport *state, GOStyle const *style, GogObject const *obj)
{
	gsf_xml_out_add_cstr (state->xml, CHART "regression-type",  "exponential");
}

static void
odf_write_power_reg (GnmOOExport *state, GOStyle const *style, GogObject const *obj)
{
	gsf_xml_out_add_cstr (state->xml, CHART "regression-type",  "power");
}

static void
odf_write_log_reg (GnmOOExport *state, GOStyle const *style, GogObject const *obj)
{
	gsf_xml_out_add_cstr (state->xml, CHART "regression-type",  "logarithmic");
}

static void
odf_write_log_fit_reg (GnmOOExport *state, GOStyle const *style, GogObject const *obj)
{
	if (state->with_extension)
		gsf_xml_out_add_cstr (state->xml, CHART "regression-type",
				      GNMSTYLE "log-fit");
}

static void
odf_write_movig_avg_reg (GnmOOExport *state, GOStyle const *style, GogObject const *obj)
{
	if (state->with_extension)
		gsf_xml_out_add_cstr (state->xml, CHART "regression-type",
				      GNMSTYLE "moving-average");
}

static void
odf_write_exp_smooth_reg (GnmOOExport *state, GOStyle const *style, GogObject const *obj)
{
	if (state->with_extension)
		gsf_xml_out_add_cstr (state->xml, CHART "regression-type",
				      GNMSTYLE "exponential-smoothed");
}

static void
odf_write_pie_point (GnmOOExport *state, GOStyle const *style, GogObject const *obj)
{
	GObjectClass *klass = G_OBJECT_GET_CLASS (obj);
	GParamSpec *spec;

	if (NULL != (spec = g_object_class_find_property (klass, "separation"))
	    && spec->value_type == G_TYPE_DOUBLE
	    && (G_PARAM_READABLE & spec->flags)) {
		double separation = 0.;
		g_object_get (G_OBJECT (obj),
			      "separation", &separation,
			      NULL);
		gsf_xml_out_add_int (state->xml,
				     CHART "pie-offset",
				     (separation * 100. + 0.5));
	}

}

static void
odf_fill_chart_props_hash (GnmOOExport *state)
{
	int i;
	static struct {
		gchar const *type;
		void (*odf_write_property) (GnmOOExport *state,
					    GOStyle const *style,
					    GogObject const *obj);
	} props[] = {
		{"GogSeriesLines", odf_write_drop},
		{"GogAxis", odf_write_axis_style},
		{"GogLinRegCurve", odf_write_lin_reg},
		{"GogPolynomRegCurve", odf_write_polynom_reg},
		{"GogExpRegCurve", odf_write_exp_reg},
		{"GogPowerRegCurve", odf_write_power_reg},
		{"GogLogRegCurve", odf_write_log_reg},
		{"GogLogFitCurve", odf_write_log_fit_reg},
		{"GogMovingAvg", odf_write_movig_avg_reg},
		{"GogExpSmooth", odf_write_exp_smooth_reg},
		{"GogPieSeriesElement", odf_write_pie_point},
		{"GogXYSeries", odf_write_interpolation_attribute},
	};

	for (i = 0 ; i < (int)G_N_ELEMENTS (props) ; i++)
		g_hash_table_insert (state->chart_props_hash, (gpointer) props[i].type,
				     props[i].odf_write_property);
}

static gboolean
_gsf_gdk_pixbuf_save (const gchar *buf,
		      gsize count,
		      GError **error,
		      gpointer data)
{
	GsfOutput *output = GSF_OUTPUT (data);
	gboolean ok = gsf_output_write (output, count, buf);

	if (!ok && error)
		*error = g_error_copy (gsf_output_error (output));

	return ok;
}

static void
odf_write_fill_images (GOImage *image, char const *name, GnmOOExport *state)
{
	GsfOutput  *child;
	char *manifest_name = g_strdup_printf ("%s/Pictures/%s.png",
					       state->object_name, name);

	child = gsf_outfile_new_child_full (state->outfile, manifest_name,
					    FALSE,
					    "compression-level", GSF_ZIP_DEFLATED,
					    NULL);

	if (child != NULL) {
		GdkPixbuf *output_pixbuf;

		state->fill_image_files
			= g_slist_prepend (state->fill_image_files,
					   manifest_name);
		output_pixbuf = go_image_get_pixbuf (image);

		gdk_pixbuf_save_to_callback (output_pixbuf,
					     _gsf_gdk_pixbuf_save,
					     child, "png",
					     NULL, NULL);
		gsf_output_close (child);
		g_object_unref (G_OBJECT (child));
	} else
		g_free (manifest_name);



}

static void
odf_write_graphs (SheetObject *graph, char const *name, GnmOOExport *state)
{
	GsfOutput  *child;

	state->object_name = name;

	child = gsf_outfile_new_child_full (state->outfile, name, TRUE,
				"compression-level", GSF_ZIP_DEFLATED,
					    NULL);
	if (NULL != child) {
		char *fullname = g_strdup_printf ("%s/content.xml", name);
		GsfOutput  *sec_child;

		state->chart_props_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
							     NULL, NULL);
		odf_fill_chart_props_hash (state);

		sec_child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
							"compression-level", GSF_ZIP_DEFLATED,
							NULL);
		if (NULL != sec_child) {
			odf_write_graph_content (state, sec_child, graph);
			gsf_output_close (sec_child);
			g_object_unref (G_OBJECT (sec_child));
		}
		g_free (fullname);

		odf_update_progress (state, 4 * state->graph_progress);

		fullname = g_strdup_printf ("%s/meta.xml", name);
		sec_child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
							"compression-level", GSF_ZIP_DEFLATED,
							NULL);
		if (NULL != sec_child) {
			odf_write_meta_graph (state, sec_child);
			gsf_output_close (sec_child);
			g_object_unref (G_OBJECT (sec_child));
		}
		g_free (fullname);
		odf_update_progress (state, state->graph_progress / 2);

		fullname = g_strdup_printf ("%s/styles.xml", name);
		sec_child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
							"compression-level", GSF_ZIP_DEFLATED,
							NULL);
		if (NULL != sec_child) {
			odf_write_graph_styles (state, sec_child);
			gsf_output_close (sec_child);
			g_object_unref (G_OBJECT (sec_child));
		}
		g_free (fullname);

		g_hash_table_foreach (state->graph_fill_images, (GHFunc) odf_write_fill_images, state);

		g_hash_table_remove_all (state->graph_dashes);
		g_hash_table_remove_all (state->graph_hatches);
		g_hash_table_remove_all (state->graph_gradients);
		g_hash_table_remove_all (state->graph_fill_images);

		g_hash_table_unref (state->chart_props_hash);
		state->chart_props_hash = NULL;
		odf_update_progress (state, state->graph_progress * (3./2.));

		gsf_output_close (child);
		g_object_unref (G_OBJECT (child));

		fullname = g_strdup_printf ("Pictures/%s", name);
		sec_child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
							"compression-level", GSF_ZIP_DEFLATED,
							NULL);
		if (NULL != sec_child) {
			GogGraph *gog = sheet_object_graph_get_gog (graph);
			if (!gog_graph_export_image (gog, GO_IMAGE_FORMAT_SVG, sec_child, 100., 100.))
				g_print ("Failed to create svg image of graph.\n");
			gsf_output_close (sec_child);
			g_object_unref (G_OBJECT (sec_child));
		}
		g_free (fullname);

		odf_update_progress (state, state->graph_progress);

		fullname = g_strdup_printf ("Pictures/%s.png", name);
		sec_child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
							"compression-level", GSF_ZIP_DEFLATED,
							NULL);
		if (NULL != sec_child) {
			GogGraph *gog = sheet_object_graph_get_gog (graph);
			if (!gog_graph_export_image (gog, GO_IMAGE_FORMAT_PNG, sec_child, 100., 100.))
				g_print ("Failed to create png image of graph.\n");
			gsf_output_close (sec_child);
			g_object_unref (G_OBJECT (sec_child));
		}
		g_free (fullname);
		odf_update_progress (state, state->graph_progress);
	}

	state->object_name = NULL;
}


/**********************************************************************************/

static void
openoffice_file_save_real (GOFileSaver const *fs, GOIOContext *ioc,
			   WorkbookView const *wbv, GsfOutput *output, gboolean with_extension)
{
	static struct {
		void (*func) (GnmOOExport *state, GsfOutput *child);
		char const *name;
	} const streams[] = {
		/* Must be first element to ensure it is not compressed */
		{ odf_write_mimetype,	"mimetype" },

		{ odf_write_content,	"content.xml" },
		{ odf_write_styles,	"styles.xml" },
		{ odf_write_meta,	"meta.xml" },
		{ odf_write_settings,	"settings.xml" },
	};

	GnmOOExport state;
	GnmLocale  *locale;
	GError *err;
	unsigned i;
	Sheet *sheet;
	GsfOutput  *pictures;
	GsfOutput  *child;

	locale  = gnm_push_C_locale ();

	state.outfile = gsf_outfile_zip_new (output, &err);

	state.with_extension = with_extension;
	state.ioc = ioc;
	state.wbv = wbv;
	state.wb  = wb_view_get_workbook (wbv);
	state.conv = odf_expr_conventions_new ();
	state.graphs = g_hash_table_new_full (g_direct_hash, g_direct_equal,
					       NULL, (GDestroyNotify) g_free);
	state.images = g_hash_table_new_full (g_direct_hash, g_direct_equal,
					       NULL, (GDestroyNotify) g_free);
	state.controls = g_hash_table_new_full (g_direct_hash, g_direct_equal,
					       NULL, (GDestroyNotify) g_free);
	state.named_cell_styles = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						   NULL, (GDestroyNotify) g_free);
	state.cell_styles = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						   NULL, (GDestroyNotify) g_free);
	state.so_styles = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						   NULL, (GDestroyNotify) g_free);
	state.xl_styles =  g_hash_table_new_full (g_str_hash, g_str_equal,
						  (GDestroyNotify) g_free, (GDestroyNotify) g_free);
	state.xl_styles_neg =  g_hash_table_new_full (g_str_hash, g_str_equal,
						  (GDestroyNotify) g_free, (GDestroyNotify) g_free);
	state.xl_styles_zero =  g_hash_table_new_full (g_str_hash, g_str_equal,
						  (GDestroyNotify) g_free, (GDestroyNotify) g_free);
	state.xl_styles_conditional =  g_hash_table_new_full (g_str_hash, g_str_equal,
						  (GDestroyNotify) g_free, (GDestroyNotify) g_free);
	state.graph_dashes = g_hash_table_new_full (g_str_hash, g_str_equal,
						    (GDestroyNotify) g_free,
						    NULL);
	state.graph_hatches = g_hash_table_new_full (g_direct_hash,
						     (GEqualFunc)odf_match_pattern,
						     NULL,
						     (GDestroyNotify) g_free);
	state.graph_gradients = g_hash_table_new_full (g_direct_hash,
						       (GEqualFunc)odf_match_gradient,
						       NULL,
						       (GDestroyNotify) g_free);
	state.graph_fill_images = g_hash_table_new_full (g_direct_hash,
							 (GEqualFunc)odf_match_image,
							 NULL,
							 (GDestroyNotify) g_free);
	state.arrow_markers = g_hash_table_new_full (g_direct_hash,
						     (GEqualFunc)odf_match_arrow_markers,
						     NULL,
						     (GDestroyNotify) g_free);
	state.col_styles = NULL;
	state.row_styles = NULL;

	state.date_long_fmt = go_format_new_from_XL ("yyyy-mm-ddThh:mm:ss");
	state.date_fmt = go_format_new_from_XL ("yyyy-mm-dd");
	state.time_fmt = go_format_new_from_XL ("\"PT0\"[h]\"H\"mm\"M\"ss\"S\"");

	state.fill_image_files = NULL;

	state.last_progress = 0;
	state.sheet_progress = ((float) PROGRESS_STEPS) / 2 /
		(workbook_sheet_count (state.wb) + G_N_ELEMENTS (streams));
	state.graph_progress = ((float) PROGRESS_STEPS) / 2;
	go_io_progress_message (state.ioc, _("Writing Sheets..."));
	go_io_value_progress_set (state.ioc, PROGRESS_STEPS, 0);



	/* ODF dos not have defaults per table, so we use our first table for defaults only.*/
	sheet = workbook_sheet_by_index (state.wb, 0);

	state.column_default = &sheet->cols.default_style;
	state.row_default = &sheet->rows.default_style;
	if (NULL != (state.default_style = sheet_style_default (sheet)))
		/* We need to make sure any referenced styles are added to the named hash */
		odf_store_this_named_style (state.default_style, "Gnumeric-default", &state);

	for (i = 0 ; i < G_N_ELEMENTS (streams); i++) {
		child = gsf_outfile_new_child_full (state.outfile, streams[i].name, FALSE,
				/* do not compress the mimetype */
				"compression-level", ((0 == i) ? GSF_ZIP_STORED : GSF_ZIP_DEFLATED),
				NULL);
		if (NULL != child) {
			streams[i].func (&state, child);
			gsf_output_close (child);
			g_object_unref (G_OBJECT (child));
		}
		odf_update_progress (&state, state.sheet_progress);
	}

	state.graph_progress = ((float) PROGRESS_STEPS) / 2 /
		(8 * g_hash_table_size (state.graphs) + g_hash_table_size (state.images) + 1);
	go_io_progress_message (state.ioc, _("Writing Sheet Objects..."));

        pictures = gsf_outfile_new_child_full (state.outfile, "Pictures", TRUE,
								"compression-level", GSF_ZIP_DEFLATED,
								NULL);
	g_hash_table_foreach (state.graphs, (GHFunc) odf_write_graphs, &state);
	g_hash_table_foreach (state.images, (GHFunc) odf_write_images, &state);
	if (NULL != pictures) {
		gsf_output_close (pictures);
		g_object_unref (G_OBJECT (pictures));
	}

	/* Need to write the manifest */
	child = gsf_outfile_new_child_full (state.outfile, "META-INF/manifest.xml", FALSE,
					    "compression-level", GSF_ZIP_DEFLATED,
					    NULL);
	if (NULL != child) {
		odf_write_manifest (&state, child);
		gsf_output_close (child);
		g_object_unref (G_OBJECT (child));
	}
	/* manifest written */

	g_free (state.conv);

	go_io_value_progress_update (state.ioc, PROGRESS_STEPS);
	go_io_progress_unset (state.ioc);
	gsf_output_close (GSF_OUTPUT (state.outfile));
	g_object_unref (G_OBJECT (state.outfile));

	gnm_pop_C_locale (locale);
	g_hash_table_unref (state.graphs);
	g_hash_table_unref (state.images);
	g_hash_table_unref (state.controls);
	g_hash_table_unref (state.named_cell_styles);
	g_hash_table_unref (state.cell_styles);
	g_hash_table_unref (state.so_styles);
	g_hash_table_unref (state.xl_styles);
	g_hash_table_unref (state.xl_styles_neg);
	g_hash_table_unref (state.xl_styles_zero);
	g_hash_table_unref (state.xl_styles_conditional);
	g_hash_table_unref (state.graph_dashes);
	g_hash_table_unref (state.graph_hatches);
	g_hash_table_unref (state.graph_gradients);
	g_hash_table_unref (state.graph_fill_images);
	g_hash_table_unref (state.arrow_markers);
	g_slist_free (state.col_styles);
	g_slist_free (state.row_styles);
	gnm_style_unref (state.default_style);
	go_format_unref (state.time_fmt);
	go_format_unref (state.date_fmt);
	go_format_unref (state.date_long_fmt);
}



void
openoffice_file_save (GOFileSaver const *fs, GOIOContext *ioc,
		      WorkbookView const *wbv, GsfOutput *output);

G_MODULE_EXPORT void
openoffice_file_save (GOFileSaver const *fs, GOIOContext *ioc,
		      WorkbookView const *wbv, GsfOutput *output)
{
	openoffice_file_save_real (fs, ioc, wbv, output, FALSE);
}

void
odf_file_save (GOFileSaver const *fs, GOIOContext *ioc,
		      WorkbookView const *wbv, GsfOutput *output);

G_MODULE_EXPORT void
odf_file_save (GOFileSaver const *fs, GOIOContext *ioc,
		      WorkbookView const *wbv, GsfOutput *output)
{
	openoffice_file_save_real (fs, ioc, wbv, output, TRUE);
}
