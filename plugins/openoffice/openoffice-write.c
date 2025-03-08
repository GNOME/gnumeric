/*
 * openoffice-write.c : export OpenOffice OASIS .ods files
 *
 * Copyright (C) 2004-2006 Jody Goldberg (jody@gnome.org)
 *
 * Copyright (C) 2006-2011 Andreas J. Guelzow (aguelzow@pyrshep.ca)
 *
 * Copyright (C) 2005 INdT - Instituto Nokia de Tecnologia
 *               Author: Luciano Wolf (luciano.wolf@indt.org.br)
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

/*****************************************************************************/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <workbook-view.h>
#include <goffice/goffice.h>
#include <gnm-format.h>
#include <workbook.h>
#include <workbook-priv.h> /* Workbook::names */
#include <cell.h>
#include <cellspan.h>
#include <sheet.h>
#include <print-info.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <sheet-merge.h>
#include <style-color.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <func.h>
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
#include <gnm-so-path.h>
#include <sheet-filter-combo.h>
#include <xml-sax.h>

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
#define TABLEOOO "tableooo:"
#define XML      "xml:"
#define CSS      "css3t:"
#define LOEXT    "loext:"
#define CALCEXT  "calcext:"
#define GNMSTYLE "gnm:"  /* We use this for attributes and elements not supported by ODF */

typedef struct {
	GsfXMLOut *xml;
	GsfOutfile *outfile;
	GOIOContext *ioc;
	WorkbookView const *wbv;
	Workbook const	   *wb;
	Sheet const	   *sheet;
	GnmConventions *conv;
	GHashTable *openformula_namemap;
	GHashTable *openformula_handlermap;
	GSList *row_styles;
	GSList *col_styles;
	GHashTable *cell_styles;
	GHashTable *named_cell_styles;
	GHashTable *named_cell_style_regions;
	GHashTable *so_styles;
	GHashTable *xl_styles;
	GHashTable *style_names[10];
	GnmStyleRegion *default_style_region;
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
	GHashTable *text_colours;
	GHashTable *font_sizes;

	gboolean with_extension;
	int odf_version;
	char *odf_version_string;
	GOFormat const *time_fmt;
	GOFormat const *date_fmt;
	GOFormat const *date_long_fmt;

	char const *object_name;
	GogView *root_view;

	/* for the manifest */
	GSList *fill_image_files; /* image/png */

	float last_progress;
	float graph_progress;
	float sheet_progress;
} GnmOOExport;

typedef struct {
	GnmConventions base;
	GnmOOExport *state;
} ODFConventions;


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
	{ "xmlns:tableooo",	"http://openoffice.org/2009/table" },
	{ "xmlns:of",		"urn:oasis:names:tc:opendocument:xmlns:of:1.2" },
	{ "xmlns:dom",		"http://www.w3.org/2001/xml-events" },
	{ "xmlns:xforms",	"http://www.w3.org/2002/xforms" },
	{ "xmlns:xsd",		"http://www.w3.org/2001/XMLSchema" },
	{ "xmlns:xsi",		"http://www.w3.org/2001/XMLSchema-instance" },
	{ "xmlns:gnm",		"http://www.gnumeric.org/odf-extension/1.0"},
	{ "xmlns:css3t",        "http://www.w3.org/TR/css3-text/"},
	{ "xmlns:loext",        "urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0"},
	{ "xmlns:calcext",      "urn:org:documentfoundation:names:experimental:calc:xmlns:calcext:1.0"},
};

/*****************************************************************************/

static void odf_write_fill_images_info (GOImage *image, char const *name, GnmOOExport *state);
static void odf_write_gradient_info (GOStyle const *style, char const *name, GnmOOExport *state);
static void odf_write_hatch_info (GOPattern *pattern, char const *name, GnmOOExport *state);
static void odf_write_dash_info (char const *name, gpointer data, GnmOOExport *state);
static void odf_write_arrow_marker_info (GOArrow const *arrow, char const *name, GnmOOExport *state);

static void odf_write_gog_style_graphic (GnmOOExport *state, GOStyle const *style, gboolean write_border);
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

typedef enum {
	OO_ITEM_TABLE_STYLE,
	OO_ITEM_TABLE_MASTER_PAGE_STYLE,
	OO_ITEM_PAGE_LAYOUT,
	OO_ITEM_UNSTYLED_GRAPH_OBJECT,
	OO_ITEM_GRAPH_STYLE,
	OO_ITEM_SHEET_OBJECT,
	OO_ITEM_SHEET_OBJECT_LINE,
	OO_ITEM_MSTYLE,
	OO_ITEM_VALIDATION,
	OO_ITEM_INPUT_MSG
} OONamedItemType;


static char *
oo_item_name (GnmOOExport *state, OONamedItemType typ, gconstpointer ptr)
{
	static const char * const
		prefixes[G_N_ELEMENTS (state->style_names)] = {
		"ta",
		"ta-mp",
		"pl",
		"GOG-",
		"GOG",
		"so-g",
		"so-g-l",
		"ACE",
		"VAL",
		"VAL-IM"
	};
	char *name;

	g_return_val_if_fail ((size_t)typ <= G_N_ELEMENTS (prefixes), NULL);

	name = g_hash_table_lookup (state->style_names[typ], ptr);
	if (name) {
		if (!g_str_has_prefix (name, prefixes[typ]))
			g_warning ("Style name confusion.");
	} else {
		name = g_strdup_printf
			("%s-%u", prefixes[typ],
			 g_hash_table_size (state->style_names[typ]));
		g_hash_table_replace (state->style_names[typ],
				      (gpointer)ptr,
				      name);
	}
	return g_strdup (name);
}


static char *
table_style_name (GnmOOExport *state, Sheet const *sheet)
{
	return oo_item_name (state, OO_ITEM_TABLE_STYLE, sheet);
}

static char *
table_master_page_style_name (GnmOOExport *state, Sheet const *sheet)
{
	return oo_item_name (state, OO_ITEM_TABLE_MASTER_PAGE_STYLE, sheet);
}

static char *
page_layout_name (GnmOOExport *state, GnmPrintInformation *pi)
{
	return oo_item_name (state, OO_ITEM_PAGE_LAYOUT, pi);
}


/*****************************************************************************/

static void
odf_write_mimetype (G_GNUC_UNUSED GnmOOExport *state, GsfOutput *child)
{
	gsf_output_puts (child, "application/vnd.oasis.opendocument.spreadsheet");
}

/*****************************************************************************/

static void
odf_add_range (GnmOOExport *state, GnmRange const *r)
{
	g_return_if_fail (range_is_sane (r));

	gsf_xml_out_add_int (state->xml, GNMSTYLE "start-col", r->start.col);
	gsf_xml_out_add_int (state->xml, GNMSTYLE "start-row", r->start.row);
	gsf_xml_out_add_int (state->xml, GNMSTYLE "end-col",   r->end.col);
	gsf_xml_out_add_int (state->xml, GNMSTYLE "end-row",   r->end.row);
}

static void
odf_add_font_weight (GnmOOExport *state, int weight)
{
	weight = ((weight+50)/100)*100;
	if (weight > 900)
		weight = 900;
	if (weight < 100)
		weight = 100;

	/* MS Excel 2007/2010 is badly confused about which weights are normal    */
	/* and/or bold, so we don't just save numbers. See                        */
	/* http://msdn.microsoft.com/en-us/library/ff528991%28v=office.12%29.aspx */
	/* although ODF refers to                                                 */
	/* http://www.w3.org/TR/2001/REC-xsl-20011015/slice7.html#font-weight     */
	/* where it is clear that 400 == normal and 700 == bold                   */
	if (weight == PANGO_WEIGHT_NORMAL)
		gsf_xml_out_add_cstr_unchecked (state->xml, FOSTYLE "font-weight",
						"normal");
	else if (weight == PANGO_WEIGHT_BOLD)
		gsf_xml_out_add_cstr_unchecked (state->xml, FOSTYLE "font-weight",
						"bold");
	else
		gsf_xml_out_add_int (state->xml, FOSTYLE "font-weight", weight);

}

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
	int spans = 0;

	switch (a->klass->type) {
	case PANGO_ATTR_FAMILY :
		break; /* ignored */
	case PANGO_ATTR_SIZE :
		{
			char * str;
			gint size = ((PangoAttrInt *)a)->value/PANGO_SCALE;
			str = g_strdup_printf ("NS-font-size%i", size);
			spans += 1;
			gsf_xml_out_start_element (state->xml, TEXT "span");
			gsf_xml_out_add_cstr (state->xml, TEXT "style-name", str);
			g_hash_table_insert (state->font_sizes,
					     str, GINT_TO_POINTER (size));
		}
		break;
	case PANGO_ATTR_RISE:
		gsf_xml_out_start_element (state->xml, TEXT "span");
		if (((PangoAttrInt *)a)->value != 0) {
			gsf_xml_out_add_cstr (state->xml, TEXT "style-name",
					      (((PangoAttrInt *)a)->value < 0)
					      ? "AC-subscript"  : "AC-superscript");
		} else
			gsf_xml_out_add_cstr (state->xml, TEXT "style-name", "AC-script");
		spans += 1;
		break;
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
		{
			PangoColor const *c;
			gchar *c_str;
			gchar *name;

			c = &((PangoAttrColor *)a)->color;
			c_str = g_strdup_printf ("#%02x%02x%02x",
						((c->red & 0xff00) >> 8),
						((c->green & 0xff00) >> 8),
						((c->blue & 0xff00) >> 8));
			name = g_strdup_printf ("NS-colour-%s", c_str + 1);
			gsf_xml_out_start_element (state->xml, TEXT "span");
			gsf_xml_out_add_cstr (state->xml, TEXT "style-name", name);
			spans += 1;
			g_hash_table_insert (state->text_colours, name, c_str);
		}
		break;
	default :
		if (a->klass->type ==
		    go_pango_attr_subscript_get_attr_type ()) {
			gsf_xml_out_start_element (state->xml, TEXT "span");
			gsf_xml_out_add_cstr (state->xml, TEXT "style-name",
					      ((GOPangoAttrSubscript *)a)->val ?
					      "AC-subscript" : "AC-script");
			spans += 1;
		} else if (a->klass->type ==
			   go_pango_attr_superscript_get_attr_type ()) {
			gsf_xml_out_start_element (state->xml, TEXT "span");
			gsf_xml_out_add_cstr (state->xml, TEXT "style-name",
					      ((GOPangoAttrSuperscript *)a)->val ?
					      "AC-superscript" : "AC-script");
			spans += 1;
		}
		break; /* ignored otherwise */
	}

	return spans;
}

static void
odf_new_markup (GnmOOExport *state, const PangoAttrList *markup, char const *text)
{
	int handled = 0;
	PangoAttrIterator * iter;
	int from, to;
	int len = text ? strlen (text) : 0;
	/* Since whitespace at the beginning of a <text:p> will be deleted upon    */
	/* reading, we need to behave as if we have already written whitespace and */
	/* use <text:s> if necessary */
	gboolean white_written = TRUE;

	if (len == 0)
		return;
	if (markup == NULL) {
		odf_add_chars (state, text, len, &white_written);
		return;
	}

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
		for (l = list; l != NULL; l = l->next) {
			PangoAttribute *a = l->data;
			spans += odf_attrs_as_string (state, a);
			pango_attribute_destroy (a);
		}
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
	go_dtoa (str, "!g", l);
	g_string_append (str, "pt");
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

static gboolean
odf_go_color_has_opacity (GOColor color)
{
	return GO_COLOR_UINT_A (color) < 255;
}

static double
odf_go_color_opacity (GOColor color)
{
	return (GO_COLOR_UINT_A (color)/255.);
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
			  char const *property, char const *id)
{
	int i;
	if (gnm_object_has_readable_prop (plot, property, G_TYPE_INT, &i))
		gsf_xml_out_add_int (xml, id, i);
}

static void
odf_write_plot_style_uint (GsfXMLOut *xml, GogObject const *plot,
			  char const *property, char const *id)
{
	unsigned int ui;
	if (gnm_object_has_readable_prop (plot, property, G_TYPE_UINT, &ui))
		gsf_xml_out_add_uint (xml, id, ui);
}

static void
odf_write_plot_style_double (GsfXMLOut *xml, GogObject const *plot,
			     char const *property, char const *id)
{
	double d;
	if (gnm_object_has_readable_prop (plot, property, G_TYPE_DOUBLE, &d))
		go_xml_out_add_double (xml, id, d);
}

static void
odf_write_plot_style_double_percent (GsfXMLOut *xml, GogObject const *plot,
				     char const *property, char const *id)
{
	double d;
	if (gnm_object_has_readable_prop (plot, property, G_TYPE_DOUBLE, &d))
		odf_add_percent (xml, id, d);
}

static void
odf_write_plot_style_bool (GsfXMLOut *xml, GogObject const *plot,
			   char const *property, char const *id)
{
	gboolean b;
	if (gnm_object_has_readable_prop (plot, property, G_TYPE_BOOLEAN, &b))
		odf_add_bool (xml, id, b);
}

static void
odf_write_plot_style_from_bool (GsfXMLOut *xml, GogObject const *plot,
				char const *property, char const *id,
				char const *t_val, char const *f_val)
{
	gboolean b;
	if (gnm_object_has_readable_prop (plot, property, G_TYPE_BOOLEAN, &b))
		gsf_xml_out_add_cstr (xml, id, b ? t_val : f_val);
}

static void
odf_start_style (GsfXMLOut *xml, char const *name, char const *family)
{
	gsf_xml_out_start_element (xml, STYLE "style");
	gsf_xml_out_add_cstr_unchecked (xml, STYLE "name", name);
	gsf_xml_out_add_cstr_unchecked (xml, STYLE "family", family);
}

static void
odf_write_table_style (GnmOOExport *state, Sheet const *sheet)
{
	char *name = table_style_name (state, sheet);
	char *mp_name  = table_master_page_style_name (state, sheet);

	odf_start_style (state->xml, name, "table");
	gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "master-page-name", mp_name);

	gsf_xml_out_start_element (state->xml, STYLE "table-properties");
	odf_add_bool (state->xml, TABLE "display",
		sheet->visibility == GNM_SHEET_VISIBILITY_VISIBLE);
	gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "writing-mode",
		sheet->text_is_rtl ? "rl-tb" : "lr-tb");
	if (state->with_extension) {
		if (state->odf_version < 103) {
			if (sheet->tab_color && !sheet->tab_color->is_auto) {
				gnm_xml_out_add_hex_color (state->xml, GNMSTYLE "tab-color",
							   sheet->tab_color, 1);
				gnm_xml_out_add_hex_color (state->xml, TABLEOOO "tab-color",
							   sheet->tab_color, 1);
			}
			if (sheet->tab_text_color && !sheet->tab_text_color->is_auto) {
				gnm_xml_out_add_hex_color (state->xml,
							   GNMSTYLE "tab-text-color",
							   sheet->tab_text_color, 1);
			}
		}
		odf_add_bool (state->xml, GNMSTYLE "display-formulas", sheet->display_formulas);
		odf_add_bool (state->xml, GNMSTYLE "display-col-header", !sheet->hide_col_header);
		odf_add_bool (state->xml, GNMSTYLE "display-row-header", !sheet->hide_row_header);
	}
	if (state->odf_version >= 103)
		gnm_xml_out_add_hex_color (state->xml, TABLE "tab-color",
					   sheet->tab_color, 1);
	gsf_xml_out_end_element (state->xml); /* </style:table-properties> */

	gsf_xml_out_end_element (state->xml); /* </style:style> */

	g_free (name);
	g_free (mp_name);
}

static gchar*
odf_get_gog_style_name (GnmOOExport *state,
			GOStyle const *style, GogObject const *obj)
{
	if (style == NULL)
		return oo_item_name (state, OO_ITEM_UNSTYLED_GRAPH_OBJECT, obj);
	else
		return oo_item_name (state, OO_ITEM_GRAPH_STYLE, style);
}

static gchar*
odf_get_gog_style_name_from_obj (GnmOOExport *state, GogObject const *obj)
{
	GOStyle *style = NULL;

	if (gnm_object_has_readable_prop (obj, "style", G_TYPE_NONE, &style)) {
		char *name = odf_get_gog_style_name (state, style, obj);
		g_object_unref (style);
		return name;
	} else
		return odf_get_gog_style_name (state, NULL, obj);
}

static const char*
xl_find_format_xl (GnmOOExport *state, char const *xl)
{
	char *found = g_hash_table_lookup (state->xl_styles, xl);

	if (found == NULL) {
		found =	g_strdup_printf ("ND-%d",
					 g_hash_table_size (state->xl_styles));
		g_hash_table_insert (state->xl_styles, g_strdup (xl), found);
	}
	return found;
}

static const char*
xl_find_format (GnmOOExport *state, GOFormat const *format)
{
	return xl_find_format_xl (state, go_format_as_XL (format));
}

static void
odf_write_table_styles (GnmOOExport *state)
{
	int i;

	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet const *sheet = workbook_sheet_by_index (state->wb, i);
		odf_write_table_style (state, sheet);
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
	char *name = oo_item_name (state, OO_ITEM_SHEET_OBJECT, so);
	GOStyle *style = NULL;

	(void)gnm_object_has_readable_prop (so, "style", G_TYPE_NONE, &style);

	odf_start_style (state->xml, name, "graphic");
	gsf_xml_out_start_element (state->xml, STYLE "graphic-properties");
	odf_add_bool (state->xml, STYLE "print-content",
		      sheet_object_get_print_flag (so));
	odf_write_gog_style_graphic (state, style, FALSE);
	gsf_xml_out_end_element (state->xml); /* </style:graphic-properties> */
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	odf_write_gog_style_text (state, style);
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	if (style != NULL)
		g_object_unref (style);
	return name;
}

static char *
odf_write_sheet_object_line_style (GnmOOExport *state, SheetObject *so)
{
	char *name = oo_item_name (state, OO_ITEM_SHEET_OBJECT_LINE, so);
	GOStyle *style = NULL;
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
	odf_add_bool (state->xml, STYLE "print-content",
		      sheet_object_get_print_flag (so));
	if (start_arrow_name != NULL) {
		gsf_xml_out_add_cstr (state->xml, DRAW "marker-start", start_arrow_name);
		odf_add_bool (state->xml, DRAW "marker-start-center", TRUE);
		odf_add_pt (state->xml, DRAW "marker-start-width",
			    start->typ == GO_ARROW_KITE ? 2 * start->c : 2 * start->a);
	}
	if (end_arrow_name != NULL) {
		gsf_xml_out_add_cstr (state->xml, DRAW "marker-end", end_arrow_name);
		odf_add_bool (state->xml, DRAW "marker-end-center", TRUE);
		odf_add_pt (state->xml, DRAW "marker-end-width",
			    end->typ == GO_ARROW_KITE ? 2 * end->c : 2 * end->a);
	}
	odf_write_gog_style_graphic (state, style, FALSE);
	gsf_xml_out_end_element (state->xml); /* </style:graphic-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	if (style != NULL)
		g_object_unref (style);
	return name;
}

static void
odf_write_sheet_object_styles (GnmOOExport *state)
{
	int i;

	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet const *sheet = workbook_sheet_by_index (state->wb, i);
		GSList *objects = sheet_objects_get (sheet, NULL, G_TYPE_NONE), *l;
		for (l = objects; l != NULL; l = l->next) {
			SheetObject *so = GNM_SO (l->data);
			char *name;
			if (GNM_IS_SO_LINE(so))
				name = odf_write_sheet_object_line_style (state, so);
			else
				name = odf_write_sheet_object_style (state, so);
			g_hash_table_replace (state->so_styles, so, name);
		}
		g_slist_free (objects);
	}
}

static void
odf_write_gog_position_pts (GnmOOExport *state, GogObject const *title)
{
	gboolean is_position_manual = TRUE;

	g_object_get (G_OBJECT (title),
		      "is-position-manual", &is_position_manual,
		      NULL);

	if (is_position_manual) {
		GogView *view = gog_view_find_child_view  (state->root_view, title);
		odf_add_pt (state->xml, SVG "x", view->allocation.x);
		odf_add_pt (state->xml, SVG "y", view->allocation.y);
	}
}

static void
odf_write_gog_position (GnmOOExport *state, GogObject const *obj)
{
	gboolean is_position_manual = TRUE;
	gchar *position = NULL, *anchor = NULL, *compass = NULL;

	if (!state->with_extension)
		return;

	(void)gnm_object_has_readable_prop (obj, "compass",
					    G_TYPE_NONE, &compass);
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
	} else if (compass)
		gsf_xml_out_add_cstr (state->xml, GNMSTYLE "compass", position);

	g_free (position);
	g_free (anchor);
	g_free (compass);
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
		if (border != NULL) {					\
			char *border_style = odf_get_border_format (border); \
			char const *gnm_border_style = odf_get_gnm_border_format (border); \
			gsf_xml_out_add_cstr_unchecked (state->xml, msbwstr, border_style); \
			g_free (border_style);				\
			if (gnm_border_style != NULL && state->with_extension) \
				gsf_xml_out_add_cstr_unchecked (state->xml, msbwstr_gnm, gnm_border_style); \
			if (border->line_type == GNM_STYLE_BORDER_DOUBLE) \
				gsf_xml_out_add_cstr_unchecked (state->xml, msbwstr_wth, "0.03cm 0.03cm 0.03cm "); \
		}							\
	}

#define UNDERLINESPECS(type, style, width, low) gsf_xml_out_add_cstr (state->xml, \
						      STYLE "text-underline-type", type); \
				gsf_xml_out_add_cstr (state->xml, \
						      STYLE "text-underline-style", style); \
				gsf_xml_out_add_cstr (state->xml, \
						      STYLE "text-underline-width", width); \
                                gsf_xml_out_add_cstr_unchecked (state->xml, \
								STYLE "text-underline-color", "font-color"); \
                                gsf_xml_out_add_cstr_unchecked (state->xml, \
								STYLE "text-underline-mode", "continuous"); \
				if (low && state->with_extension) \
					gsf_xml_out_add_cstr_unchecked (state->xml, \
									GNMSTYLE "text-underline-placement", "low"); \

static void
odf_write_style_cell_properties (GnmOOExport *state, GnmStyle const *style)
{
	gboolean test1, test2;

	gsf_xml_out_start_element (state->xml, STYLE "table-cell-properties");
/* Background Color */
	if (gnm_style_is_element_set (style, MSTYLE_COLOR_BACK)) {
		gboolean pattern_set = gnm_style_is_element_set (style, MSTYLE_PATTERN);
		int pattern = pattern_set ? gnm_style_get_pattern (style) : 1;

		gnm_xml_out_add_hex_color (state->xml, FOSTYLE "background-color",
					   gnm_style_get_back_color (style), pattern);
		if (state->with_extension) {
			/* We save this to retain as much state as possible. */
			gnm_xml_out_add_hex_color (state->xml, GNMSTYLE "background-colour",
						   gnm_style_get_back_color (style), 1);
			if (gnm_style_is_element_set (style, MSTYLE_COLOR_PATTERN))
				gnm_xml_out_add_hex_color (state->xml, GNMSTYLE "pattern-colour",
							   gnm_style_get_pattern_color (style), 1);
			gsf_xml_out_add_int (state->xml, GNMSTYLE "pattern", pattern);
		}
	}
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
		case GNM_VALIGN_TOP:
			alignment = "top";
			break;
		case GNM_VALIGN_BOTTOM:
			alignment= "bottom";
			break;
		case GNM_VALIGN_CENTER:
			alignment = "middle";
			break;
		case GNM_VALIGN_JUSTIFY:
		case GNM_VALIGN_DISTRIBUTED:
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
		if (state->odf_version > 101)
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

/* Decimal Places (this is the maximum number of decimal places shown if not otherwise specified.)  */
	/* Only interpreted in a default style. */
	gsf_xml_out_add_int (state->xml, STYLE "decimal-places", 13);

/* Horizontal Alignment */
	if (gnm_style_is_element_set (style, MSTYLE_ALIGN_H)) {
		GnmHAlign align = gnm_style_get_align_h (style);
		char const *source = NULL;
		gboolean rep_content = FALSE;

		switch (align) {
		case GNM_HALIGN_DISTRIBUTED:
		case GNM_HALIGN_LEFT:
		case GNM_HALIGN_RIGHT:
		case GNM_HALIGN_CENTER:
		case GNM_HALIGN_JUSTIFY:
		case GNM_HALIGN_CENTER_ACROSS_SELECTION:
		        source = "fix";
			break;
		case GNM_HALIGN_FILL:
			rep_content = TRUE;
		case GNM_HALIGN_GENERAL:
		default:
			/* Note that since source is value-type, alignment should be ignored */
                        /*(but isn't by OOo) */
			source = "value-type";
			break;
		}
		gsf_xml_out_add_cstr (state->xml, STYLE "text-align-source", source);
		/* Repeat Content */
		odf_add_bool (state->xml,  STYLE "repeat-content", rep_content);
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
		case GNM_HALIGN_LEFT:
			alignment = "left";
			break;
		case GNM_HALIGN_RIGHT:
			alignment= "right";
			break;
		case GNM_HALIGN_CENTER:
			alignment = "center";
			break;
		case GNM_HALIGN_JUSTIFY:
			alignment = "justify";
			break;
		case GNM_HALIGN_DISTRIBUTED:
			alignment = "justify";
			gnum_specs = TRUE;
			break;
		case GNM_HALIGN_FILL:
			/* handled by repeat-content */
			break;
		case GNM_HALIGN_CENTER_ACROSS_SELECTION:
			alignment = "center";
			gnum_specs = TRUE;
			break;
		case GNM_HALIGN_GENERAL:
		default:
			/* Note that since source is value-type, alignment should be ignored */
                        /*(but isn't by OOo) */
			alignment = "start";
			gnum_specs = TRUE;
			break;
		}
		if (align != GNM_HALIGN_GENERAL && align != GNM_HALIGN_FILL)
			gsf_xml_out_add_cstr (state->xml, FOSTYLE "text-align", alignment);
		if (state->with_extension) {
			if (gnum_specs)
				gsf_xml_out_add_int (state->xml, GNMSTYLE "GnmHAlign", align);
			if (align == GNM_HALIGN_DISTRIBUTED)
				gsf_xml_out_add_cstr (state->xml, CSS "text-justify", "distribute");
		}
	}

/* Text Indent */
	if (gnm_style_is_element_set (style, MSTYLE_INDENT))
		odf_add_pt (state->xml, FOSTYLE "margin-left", gnm_style_get_indent (style));

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
		odf_add_font_weight (state,
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
			UNDERLINESPECS("none", "none", "auto", FALSE);
			break;
		case UNDERLINE_SINGLE:
			UNDERLINESPECS("single", "solid", "auto", FALSE);
			break;
		case UNDERLINE_DOUBLE:
			UNDERLINESPECS("double", "solid", "auto", FALSE);
			break;
		case UNDERLINE_SINGLE_LOW:
			UNDERLINESPECS("single", "dash", "auto", TRUE);
			break;
		case UNDERLINE_DOUBLE_LOW:
			UNDERLINESPECS("double", "dash", "auto", TRUE);
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
	else
		name = xl_find_format (state, gof);

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
		g_printerr ("Could not find style %p\n", style);
		return NULL;
	}

	return found;
}

static void
odf_save_style_map_single_f (GnmOOExport *state, GString *str, GnmExprTop const *texpr, GnmParsePos *pp)
{
	char *formula;

	formula = gnm_expr_top_as_string (texpr, pp, state->conv);
	g_string_append (str, formula);
	g_free (formula);
}


static void
odf_save_style_map_double_f (GnmOOExport *state, GString *str, GnmStyleCond const *cond, GnmParsePos *pp)
{
	g_string_append_c (str, '(');
	odf_save_style_map_single_f (state, str, gnm_style_cond_get_expr (cond, 0), pp);
	g_string_append_c (str, ',');
	odf_save_style_map_single_f (state, str, gnm_style_cond_get_expr (cond, 1), pp);
	g_string_append_c (str, ')');
}

static char *
odf_strip_brackets (char *string)
{
	char *closing;
	closing = strrchr(string, ']');
	if (closing != NULL && *(closing+1) == '\0')
		*closing = '\0';
	return ((*string == '[') ? (string + 1) : string);
}

static void
odf_determine_base (GnmOOExport *state, GnmRange *r, GnmParsePos *pp)
{
	if (r)
		parse_pos_init (pp, (Workbook *) state->wb, state->sheet, r->start.col, r->start.row);
	else {
		g_warning ("Unable to determine an appropriate base cell address.");
		parse_pos_init (pp, (Workbook *) state->wb, state->sheet, 0, 0);
	}
}

static void
odf_save_style_map (GnmOOExport *state, GnmStyleCond const *cond, GnmRange *r)
{
	char const *name = odf_find_style (state, cond->overlay);
	GString *str;
	gchar *address;
	GnmExprTop const *texpr = NULL;
	GnmCellRef ref;
	GnmParsePos pp;
	GnmStyleCondOp op = cond->op;

	g_return_if_fail (name != NULL);

	str = g_string_new (NULL);

	switch (op) {
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
		texpr = gnm_style_cond_get_alternate_expr (cond);
		op = GNM_STYLE_COND_CUSTOM;
		break;
	default:
		break;
	}

	switch (op) {
	case GNM_STYLE_COND_BETWEEN:
		odf_determine_base (state, r, &pp);
		g_string_append (str, "of:cell-content-is-between");
		odf_save_style_map_double_f (state, str, cond, &pp);
		break;
	case GNM_STYLE_COND_NOT_BETWEEN:
		odf_determine_base (state, r, &pp);
		g_string_append (str, "of:cell-content-is-not-between");
		odf_save_style_map_double_f (state, str, cond, &pp);
		break;
	case GNM_STYLE_COND_EQUAL:
		odf_determine_base (state, r, &pp);
		g_string_append (str, "of:cell-content()=");
		odf_save_style_map_single_f (state, str, gnm_style_cond_get_expr (cond, 0), &pp);
		break;
	case GNM_STYLE_COND_NOT_EQUAL:
		odf_determine_base (state, r, &pp);
		g_string_append (str, "of:cell-content()!=");
		odf_save_style_map_single_f (state, str, gnm_style_cond_get_expr (cond, 0), &pp);
		break;
	case GNM_STYLE_COND_GT:
		odf_determine_base (state, r, &pp);
		g_string_append (str, "of:cell-content()>");
		odf_save_style_map_single_f (state, str, gnm_style_cond_get_expr (cond, 0), &pp);
		break;
	case GNM_STYLE_COND_LT:
		odf_determine_base (state, r, &pp);
		g_string_append (str, "of:cell-content()<");
		odf_save_style_map_single_f (state, str, gnm_style_cond_get_expr (cond, 0), &pp);
		break;
	case GNM_STYLE_COND_GTE:
		odf_determine_base (state, r, &pp);
		g_string_append (str, "of:cell-content()>=");
		odf_save_style_map_single_f (state, str, gnm_style_cond_get_expr (cond, 0), &pp);
		break;
	case GNM_STYLE_COND_LTE:
		odf_determine_base (state, r, &pp);
		g_string_append (str, "of:cell-content()<=");
		odf_save_style_map_single_f (state, str, gnm_style_cond_get_expr (cond, 0), &pp);
		break;
	case GNM_STYLE_COND_CUSTOM:
		odf_determine_base (state, r, &pp);
		g_string_append (str, "of:is-true-formula(");
		odf_save_style_map_single_f (state, str, texpr ? texpr : gnm_style_cond_get_expr (cond, 0), &pp);
		g_string_append (str, ")");
		break;
	default:
		g_string_free (str, TRUE);
		g_warning ("Unknown style condition %d", op);
		return;
	}

	if (texpr) {
		gnm_expr_top_unref (texpr);
		texpr = NULL;
	}

	gsf_xml_out_start_element (state->xml, STYLE "map");

	gsf_xml_out_add_cstr (state->xml, STYLE "apply-style-name", name);
	gsf_xml_out_add_cstr (state->xml, STYLE "condition", str->str);

	/* ODF 1.2 requires a sheet name for the base-cell-address */
	/* This is really only needed if we include a formula      */
	gnm_cellref_init (&ref, (Sheet *)state->sheet,
			  pp.eval.col, pp.eval.row, FALSE);
	texpr =  gnm_expr_top_new (gnm_expr_new_cellref (&ref));
	parse_pos_init_sheet (&pp, state->sheet);
	address = gnm_expr_top_as_string (texpr, &pp, state->conv);
	gsf_xml_out_add_cstr (state->xml, STYLE "base-cell-address", odf_strip_brackets (address));
	g_free (address);
	gnm_expr_top_unref (texpr);

	gsf_xml_out_end_element (state->xml); /* </style:map> */

	g_string_free (str, TRUE);
}

static void
odf_write_style (GnmOOExport *state, GnmStyle const *style, GnmRange *r, gboolean is_default)
{
	GnmStyleConditions const *sc;
	GPtrArray const *conds;
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
	    NULL != (conds = gnm_style_conditions_details (sc))) {
		for (i = 0 ; i < conds->len ; i++) {
			GnmStyleCond const *cond = g_ptr_array_index (conds, i);
			odf_save_style_map (state, cond, r);
		}
	}

/* MSTYLE_VALIDATION validations need to be written at a different place and time in ODF  */
/* MSTYLE_HLINK hyperlinks cannot be attached to styles but need to be attached to the cell content */
}

#undef UNDERLINESPECS
#undef BORDERSTYLE

static gint
odf_compare_ci (gconstpointer a, gconstpointer b)
{
	col_row_styles_t const *old_style = a;
	ColRowInfo const *new_style = b;

	return !col_row_info_equal (new_style, old_style->ci);
}

static void
col_row_styles_free (gpointer data)
{
	col_row_styles_t *style = data;

	if (data) {
		g_free (style->name);
		g_free (style);
	}
}

static void
odf_write_row_style (GnmOOExport *state, ColRowInfo const *ci)
{
	gsf_xml_out_start_element (state->xml, STYLE "table-row-properties");
	odf_add_pt (state->xml, STYLE "row-height", ci->size_pts);
	odf_add_bool (state->xml, STYLE "use-optimal-row-height",
		      !ci->hard_size);
	gsf_xml_out_end_element (state->xml); /* </style:table-row-properties> */
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
odf_save_this_style_with_name (GnmStyleRegion *sr, char const *name, GnmOOExport *state)
{
	odf_start_style (state->xml, name, "table-cell");
	odf_write_style (state, sr->style, &sr->range, FALSE);
	gsf_xml_out_end_element (state->xml); /* </style:style */
}

static void
odf_store_this_named_style (GnmStyle *style, char const *name, GnmRange *r, GnmOOExport *state)
{
	char *real_name = NULL;
	const char *old_name;
	GnmStyleConditions const *sc;

	old_name = g_hash_table_lookup (state->named_cell_styles, style);

	if (name) {
		if (old_name)
			g_warning ("Unexpected style name reuse.");
		real_name = g_strdup (name);
	} else if (!old_name) {
		int i = g_hash_table_size (state->named_cell_styles);
                /* All styles referenced by a style:map need to be named, so in that case */
		/* we make up a name, that ought to look nice */
		real_name = g_strdup_printf ("Gnumeric-%i", i);
	}

	if (!old_name)
		g_hash_table_insert (state->named_cell_styles, style, real_name);

	g_hash_table_insert (state->named_cell_style_regions, gnm_style_region_new (r, style),
			     g_strdup (old_name ? old_name : real_name));

	if (gnm_style_is_element_set (style, MSTYLE_CONDITIONS) &&
	    NULL != (sc = gnm_style_get_conditions (style))) {
		GPtrArray const *conds = gnm_style_conditions_details (sc);
		if (conds != NULL) {
			guint i;
			for (i = 0 ; i < conds->len ; i++) {
				GnmStyleCond const *cond =
					g_ptr_array_index (conds, i);
				odf_store_this_named_style (cond->overlay, NULL, r, state);
			}
		}
	}
}

static void
odf_save_this_style (G_GNUC_UNUSED gconstpointer dummy, GnmStyleRegion *sr, GnmOOExport *state)
{
	char *name;
	GnmStyleConditions const *sc;

	if (NULL != g_hash_table_lookup (state->cell_styles, sr->style))
		return;

	name = oo_item_name (state, OO_ITEM_MSTYLE, sr->style);
	g_hash_table_insert (state->cell_styles, sr->style, name);

	if (gnm_style_is_element_set (sr->style, MSTYLE_CONDITIONS) &&
	    NULL != (sc = gnm_style_get_conditions (sr->style))) {
		GPtrArray const *conds = gnm_style_conditions_details (sc);
		if (conds != NULL) {
			guint i;
			for (i = 0 ; i < conds->len ; i++) {
				GnmStyleCond const *cond =
					g_ptr_array_index (conds, i);
				odf_store_this_named_style (cond->overlay, NULL, &sr->range, state);
			}
		}
	}

	odf_save_this_style_with_name (sr, name, state);
}

static void
odf_write_text_colours (char const *name, G_GNUC_UNUSED gpointer data, GnmOOExport *state)
{
	char const *colour = data;
	char *display = g_strdup_printf ("Font Color %s", colour);
	odf_start_style (state->xml, name, "text");
	gsf_xml_out_add_cstr (state->xml, STYLE "display-name", display);
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, FOSTYLE "color", colour);
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */
	g_free (display);
}

static void
odf_write_font_sizes (gpointer key, gpointer value, gpointer user_data)
{
	GnmOOExport *state = user_data;
	gint i = GPOINTER_TO_INT (value);
	char * str = key;
	char *display = g_strdup_printf ("Font Size %ipt", i);
	odf_start_style (state->xml, str, "text");
	gsf_xml_out_add_cstr (state->xml, STYLE "display-name", display);
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	odf_add_pt (state->xml, FOSTYLE "font-size", (double) i);
	odf_add_pt (state->xml, STYLE "font-size-asian", (double) i);
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */
	g_free (display);
}

static void
odf_write_character_styles (GnmOOExport *state)
{
	int i;

	for (i = 100; i <= 1000; i+=100) {
		char * str = g_strdup_printf ("AC-weight%i", i);
		odf_start_style (state->xml, str, "text");
		gsf_xml_out_start_element (state->xml, STYLE "text-properties");
		odf_add_font_weight (state, i);
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
	gsf_xml_out_add_cstr (state->xml, STYLE "text-position", "sub 83%");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "AC-superscript", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-position", "super 83%");
	gsf_xml_out_end_element (state->xml); /* </style:text-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "AC-script", "text");
	gsf_xml_out_start_element (state->xml, STYLE "text-properties");
	gsf_xml_out_add_cstr (state->xml, STYLE "text-position", "0% 100%");
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
		sheet_style_range_foreach (state->sheet, NULL,
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
			if (!col_row_info_equal (last_ci, this_ci))
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
			if (!col_row_info_equal (last_ci, this_ci))
				odf_find_row_style (state, (last_ci = this_ci), TRUE);
		}
	}

	return;
}

static void
odf_print_string (GnmConventionsOut *out, char const *str, char quote)
{
	GString *target = out->accum;

	/* Strings are surrounded by quote characters; a literal quote character '"'*/
	/* as string content is escaped by duplicating it. */

	g_string_append_c (target, quote);
	/* This loop should be UTF-8 safe.  */
	for (; *str; str++) {
		g_string_append_c (target, *str);
		if (*str == quote)
			g_string_append_c (target, quote);
	}
	g_string_append_c (target, quote);

}

static void
odf_cellref_as_string_base (GnmConventionsOut *out,
		       GnmCellRef const *cell_ref,
		       gboolean no_sheetname)
{
	GString *target = out->accum;
	GnmCellPos pos;
	Sheet const *sheet = cell_ref->sheet;
	Sheet const *size_sheet = eval_sheet (sheet, out->pp->sheet);
	GnmSheetSize const *ss =
		gnm_sheet_get_size2 (size_sheet, out->pp->wb);

	if (sheet != NULL && !no_sheetname) {
		if (NULL != out->pp->wb && sheet->workbook != out->pp->wb) {
			char const *ext_ref;
			ext_ref = go_doc_get_uri ((GODoc *)(sheet->workbook));
			odf_print_string (out, ext_ref, '\'');
			g_string_append_c (target, '#');
		}
		g_string_append_c (target, '$');
		odf_print_string (out, sheet->name_unquoted, '\'');
	}
	g_string_append_c (target, '.');

	gnm_cellpos_init_cellref_ss (&pos, cell_ref, &out->pp->eval, ss);

	if (!cell_ref->col_relative)
		g_string_append_c (target, '$');
	g_string_append (target, col_name (pos.col));

	if (!cell_ref->row_relative)
		g_string_append_c (target, '$');
	g_string_append (target, row_name (pos.row));

}

static void
odf_cellref_as_string (GnmConventionsOut *out,
		       GnmCellRef const *cell_ref,
		       gboolean no_sheetname)
{
	g_string_append (out->accum, "[");
	odf_cellref_as_string_base (out, cell_ref, no_sheetname);
	g_string_append (out->accum, "]");
}

static void
odf_rangeref_as_string (GnmConventionsOut *out, GnmRangeRef const *ref)
{
	g_string_append (out->accum, "[");
	odf_cellref_as_string_base (out, &(ref->a), FALSE);
	g_string_append_c (out->accum, ':');
	odf_cellref_as_string_base (out, &(ref->b), ref->b.sheet == ref->a.sheet);
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

		/* The following are functions that exist in OpenFormula or with another */
		/* known prefix. This listing is */
		/* alphabetical by the second entry, the OpenFormula or foreign name (w/o prefix). */

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
		{ "AVERAGEIFS","AVERAGEIFS" },
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
		{ "CONFIDENCE.T","COM.MICROSOFT.CONFIDENCE.T" },
		{ "COMBIN","COMBIN" },
		{ "COMBINA","COMBINA" },
		{ "COMPLEX","COMPLEX" },
		{ "CONCAT","COM.MICROSOFT.CONCAT" },
		{ "CONCATENATE","COM.MICROSOFT.CONCAT" },
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
		{ "COUNTIFS","COUNTIFS" },
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
		{ "IFS","COM.MICROSOFT.IFS" },
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
		{ "TDIST","LEGACY.TDIST" },
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
		{ "MAXIFS","COM.MICROSOFT.MAXIFS" },
		{ "MDETERM","MDETERM" },
		{ "MDURATION","MDURATION" },
		{ "MEDIAN","MEDIAN" },
		{ "MID","MID" },
		{ "MIDB","MIDB" },
		{ "MIN","MIN" },
		{ "MINA","MINA" },
		{ "MINIFS","COM.MICROSOFT.MINIFS" },
		{ "MINUTE","MINUTE" },
		{ "MINVERSE","MINVERSE" },
		{ "MIRR","MIRR" },
		{ "MMULT","MMULT" },
		{ "MOD","MOD" },
		{ "MODE","MODE" },
		{ "MODE.MULT","COM.MICROSOFT.MODE.MULT" },
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
		{ "PERCENTILE.EXC","COM.MICROSOFT.PERCENTILE.EXC" },
		{ "PERCENTRANK","PERCENTRANK" },
		{ "PERCENTRANK.EXC","COM.MICROSOFT.PERCENTRANK.EXC" },
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
		{ "QUARTILE.EXC","COM.MICROSOFT.QUARTILE.EXC" },
		{ "QUOTIENT","QUOTIENT" },
		{ "RADIANS","RADIANS" },
		{ "RAND","RAND" },
		{ "RANDBETWEEN","RANDBETWEEN" },
		{ "RANK","RANK" },
		{ "RANK.AVG","COM.MICROSOFT.RANK.AVG" },
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
		{ "SUMIFS","SUMIFS" },
		{ "ODF.SUMPRODUCT","SUMPRODUCT" },
		{ "SUMSQ","SUMSQ" },
		{ "SUMX2MY2","SUMX2MY2" },
		{ "SUMX2PY2","SUMX2PY2" },
		{ "SUMXMY2","SUMXMY2" },
		{ "SWITCH", "COM.MICROSOFT.SWITCH" },
		{ "SYD","SYD" },
		{ "T","T" },
		{ "TAN","TAN" },
		{ "TANH","TANH" },
		{ "TBILLEQ","TBILLEQ" },
		{ "TBILLPRICE","TBILLPRICE" },
		{ "TBILLYIELD","TBILLYIELD" },
		{ "TEXT","TEXT" },
		{ "TEXTJOIN","COM.MICROSOFT.TEXTJOIN" },
		{ "ODF.TIME","TIME" },
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
	ODFConventions *oconv = (ODFConventions *)(out->convs);
	GHashTable *namemap;
	GHashTable *handlermap;

	char const *name = gnm_func_get_name (func->func, FALSE);
	gboolean (*handler) (GnmConventionsOut *out, GnmExprFunction const *func);

	if (NULL == oconv->state->openformula_namemap) {
		guint i;
		namemap = g_hash_table_new (go_ascii_strcase_hash,
					    go_ascii_strcase_equal);
		for (i = 0; sc_func_renames[i].gnm_name; i++)
			g_hash_table_insert (namemap,
					     (gchar *) sc_func_renames[i].gnm_name,
					     (gchar *) sc_func_renames[i].odf_name);
		oconv->state->openformula_namemap = namemap;
	} else
		namemap = oconv->state->openformula_namemap;

	if (NULL == oconv->state->openformula_handlermap) {
		guint i;
		handlermap = g_hash_table_new (go_ascii_strcase_hash,
					       go_ascii_strcase_equal);
		for (i = 0; sc_func_handlers[i].gnm_name; i++)
			g_hash_table_insert (handlermap,
					     (gchar *) sc_func_handlers[i].gnm_name,
					     sc_func_handlers[i].handler);
		oconv->state->openformula_handlermap = handlermap;
	} else
		handlermap = oconv->state->openformula_handlermap;

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

static void
odf_string_handler (GnmConventionsOut *out, GOString const *str)
{
	/* Constant strings are surrounded by double-quote characters */
	/* (QUOTATION MARK, U+0022); a literal double-quote character '"'*/
	/* (QUOTATION MARK, U+0022) as */
	/* string content is escaped by duplicating it. */

	odf_print_string (out, str->str, '"');
}

static void
odf_boolean_handler (GnmConventionsOut *out, gboolean val)
{
	g_string_append (out->accum, val ? "TRUE()" : "FALSE()");
}


static GnmConventions *
odf_expr_conventions_new (GnmOOExport *state)
{
	GnmConventions *conv = gnm_conventions_new_full
		(sizeof (ODFConventions));
	ODFConventions *oconv = (ODFConventions *)conv;

	conv->sheet_name_sep		= '.';
	conv->arg_sep			= ';';
	conv->array_col_sep		= ';';
	conv->array_row_sep		= '|';
	conv->intersection_char         = '!';
	conv->decimal_sep_dot		= TRUE;
	conv->output.string		= odf_string_handler;
	conv->output.cell_ref		= odf_cellref_as_string;
	conv->output.range_ref		= odf_rangeref_as_string;
	conv->output.func               = odf_expr_func_handler;
	conv->output.boolean            = odf_boolean_handler;
	conv->output.uppercase_E        = FALSE;

	if (!gnm_shortest_rep_in_files ()) {
		gnm_float l10 = gnm_log10 (GNM_RADIX);
		conv->output.decimal_digits  =
			(int)gnm_ceil (GNM_MANT_DIG * l10) +
			(l10 == (int)l10 ? 0 : 1);
	}

	oconv->state                    = state;

	return conv;
}

static gboolean
odf_cell_is_covered (G_GNUC_UNUSED Sheet const *sheet,
		     G_GNUC_UNUSED GnmCell *current_cell,
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
	char *author = NULL;
	char *text = NULL;
	PangoAttrList * markup = NULL;
	gboolean pp = TRUE;

	g_object_get (G_OBJECT (state->xml), "pretty-print", &pp, NULL);

	g_object_get (G_OBJECT (cc), "text", &text,
		      "markup", &markup, "author", &author,  NULL);

	gsf_xml_out_start_element (state->xml, OFFICE "annotation");
	if (author != NULL) {
		gsf_xml_out_start_element (state->xml, DUBLINCORE "creator");
		gsf_xml_out_add_cstr (state->xml, NULL, author);
		gsf_xml_out_end_element (state->xml); /*  DUBLINCORE "creator" */;
		g_free (author);
	}
	if (text != NULL) {
		g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
		gsf_xml_out_start_element (state->xml, TEXT "p");
		odf_new_markup (state, markup, text);
		gsf_xml_out_end_element (state->xml);   /* p */
		g_free (text);
		if (markup != NULL)
			pango_attr_list_unref (markup);

	}
	g_object_set (G_OBJECT (state->xml), "pretty-print", pp, NULL);
	gsf_xml_out_end_element (state->xml); /*  OFFICE "annotation" */
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
	Sheet *sheet;
	char *name = NULL;

	sheet_object_anchor_to_offset_pts (anchor, state->sheet, res_pts);

	switch (anchor->mode) {
	case GNM_SO_ANCHOR_TWO_CELLS:
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
		break;
	case GNM_SO_ANCHOR_ONE_CELL:
		odf_add_pt (state->xml, SVG "x", res_pts[0]);
		odf_add_pt (state->xml, SVG "y", res_pts[1]);
		odf_add_pt (state->xml, SVG "width", anchor->offset[2]);
		odf_add_pt (state->xml, SVG "height", anchor->offset[3]);
		break;
	case GNM_SO_ANCHOR_ABSOLUTE:
		odf_add_pt (state->xml, SVG "x", anchor->offset[0]);
		odf_add_pt (state->xml, SVG "y", anchor->offset[1]);
		odf_add_pt (state->xml, SVG "width", anchor->offset[2]);
		odf_add_pt (state->xml, SVG "height", anchor->offset[3]);
		break;
	}

	g_object_get (G_OBJECT (so),
			      "name", &name,
			      NULL);
	if (name) {
	        gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "name", name);
		g_free (name);
	}

	sheet = sheet_object_get_sheet (so);
	if (sheet) {
		int z;
		z = g_slist_length (sheet->sheet_objects)
			- sheet_object_get_stacking (so);
		gsf_xml_out_add_int (state->xml, DRAW "z-index", z);
	}
}

static void
odf_write_multi_chart_frame_size (GnmOOExport *state, SheetObject *so, GogObject *obj, guint tr, guint tc)
{
	SheetObjectAnchor const *anchor = sheet_object_get_anchor (so);
	double abs_pts[4] = {0.,0.,0.,0.};
	double off_pts[4] = {0.,0.,0.,0.};
	double res_pts[4] = {0.,0.,0.,0.};
	GnmRange const *r = &anchor->cell_bound;
	GnmCellRef ref;
	GnmExprTop const *texpr;
	GnmParsePos pp;
	char *formula;
	Sheet const *sheet = state->sheet;
	unsigned int xpos = 0, ypos = 0, columns = 1, rows = 1;
	double height, width;

	if (!gog_chart_get_position (GOG_CHART (obj),
			&xpos, &ypos, &columns, &rows)) {
		odf_write_frame_size (state, so);
		return;
	}

	sheet_object_anchor_to_pts (anchor, sheet, abs_pts);
	sheet_object_anchor_to_offset_pts (anchor, sheet, off_pts);

	res_pts[0] = off_pts[0] + ((tc == 0) ? 0 : (xpos * (abs_pts[2]-abs_pts[0])/tc));
	res_pts[1] = off_pts[1] + ((tr == 0) ? 0 : (ypos * (abs_pts[3]-abs_pts[1])/tr));
	res_pts[2] = off_pts[0] + ((tc == 0) ? (abs_pts[2]-abs_pts[0]) :
				   ((xpos + columns) * (abs_pts[2]-abs_pts[0])/tc));
	res_pts[3] = off_pts[1] + ((tr == 0) ? (abs_pts[3]-abs_pts[1]) :
				   ((ypos + rows) * (abs_pts[3]-abs_pts[1])/tr));
	width = res_pts[2] - res_pts[0];
	height = res_pts[3] - res_pts[1];

	res_pts[2] -= sheet_col_get_distance_pts (sheet, r->start.col,
						  r->end.col);
	res_pts[3] -= sheet_row_get_distance_pts (sheet, r->start.row,
						  r->end.row);

	odf_add_pt (state->xml, SVG "x", res_pts[0]);
	odf_add_pt (state->xml, SVG "y", res_pts[1]);
	odf_add_pt (state->xml, TABLE "end-x", res_pts[2]);
	odf_add_pt (state->xml, TABLE "end-y", res_pts[3]);

	odf_add_pt (state->xml, SVG "width", width);
	odf_add_pt (state->xml, SVG "height", height);


	gnm_cellref_init (&ref, (Sheet *) sheet, r->end.col, r->end.row, TRUE);
	texpr =  gnm_expr_top_new (gnm_expr_new_cellref (&ref));
	parse_pos_init_sheet (&pp, state->sheet);
	formula = gnm_expr_top_as_string (texpr, &pp, state->conv);
	gnm_expr_top_unref (texpr);
	gsf_xml_out_add_cstr (state->xml, TABLE "end-cell-address",
			      odf_strip_brackets (formula));
	g_free (formula);

	if (sheet) {
		int z;
		z = g_slist_length (sheet->sheet_objects)
			- sheet_object_get_stacking (so);
		gsf_xml_out_add_int (state->xml, DRAW "z-index", z);
	}
}

static guint
odf_n_charts (GnmOOExport *state, SheetObject *so)
{
	GogGraph const	*graph = sheet_object_graph_get_gog (so);
	GogObjectRole const *role = gog_object_find_role_by_name (GOG_OBJECT (graph), "Chart");
	GSList *list = gog_object_get_children (GOG_OBJECT (graph), role);
	guint n = g_slist_length (list);
	g_slist_free (list);
	return n;
}

static void
odf_write_graph (GnmOOExport *state, SheetObject *so, char const *name,
		 char const *style_name)
{
	GnmParsePos pp;

	parse_pos_init_sheet (&pp, state->sheet);

	if (name != NULL) {
		GogGraph *graph = sheet_object_graph_get_gog (so);
		GogObjectRole const *role = gog_object_find_role_by_name (GOG_OBJECT (graph), "Chart");
		GSList *list = gog_object_get_children (GOG_OBJECT (graph), role);
		if (list != NULL) {
			GSList *l = list;
			gboolean multichart = (NULL != list->next);
			char *series_name = odf_graph_get_series (state, graph, &pp);
			guint i = 0, total_rows, total_columns;

			if (multichart) {
				total_columns = gog_graph_num_cols (graph);
				total_rows = gog_graph_num_rows (graph);
			}

			while  (l) {
				char *full_name = g_strdup_printf ("%s-%i/", name, i);
				gsf_xml_out_start_element (state->xml, DRAW "frame");
				if (style_name != NULL)
					gsf_xml_out_add_cstr (state->xml, DRAW "style-name", style_name);
				if (multichart)
					odf_write_multi_chart_frame_size (state, so, GOG_OBJECT (l->data),
									  total_rows, total_columns);
				else
					odf_write_frame_size (state, so);
				gsf_xml_out_start_element (state->xml, DRAW "object");
				gsf_xml_out_add_cstr (state->xml, XLINK "href", full_name);
				g_free (full_name);
				gsf_xml_out_add_cstr (state->xml, XLINK "type", "simple");
				gsf_xml_out_add_cstr (state->xml, XLINK "show", "embed");
				gsf_xml_out_add_cstr (state->xml, XLINK "actuate", "onLoad");
				gsf_xml_out_add_cstr (state->xml, DRAW "notify-on-update-of-ranges",
						      series_name);
				gsf_xml_out_end_element (state->xml); /*  DRAW "object" */
				full_name = g_strdup_printf ("Pictures/%s-%i", name, i);
				gsf_xml_out_start_element (state->xml, DRAW "image");
				gsf_xml_out_add_cstr (state->xml, XLINK "href", full_name);
				g_free (full_name);
				gsf_xml_out_add_cstr (state->xml, XLINK "type", "simple");
				gsf_xml_out_add_cstr (state->xml, XLINK "show", "embed");
				gsf_xml_out_add_cstr (state->xml, XLINK "actuate", "onLoad");
				gsf_xml_out_end_element (state->xml); /*  DRAW "image" */
				full_name = g_strdup_printf ("Pictures/%s-%i.png", name,i);
				gsf_xml_out_start_element (state->xml, DRAW "image");
				gsf_xml_out_add_cstr (state->xml, XLINK "href", full_name);
				g_free (full_name);
				gsf_xml_out_add_cstr (state->xml, XLINK "type", "simple");
				gsf_xml_out_add_cstr (state->xml, XLINK "show", "embed");
				gsf_xml_out_add_cstr (state->xml, XLINK "actuate", "onLoad");
				gsf_xml_out_end_element (state->xml); /*  DRAW "image" */
				gsf_xml_out_end_element (state->xml); /*  DRAW "frame" */
				i++;
				l = l->next;
			}
			g_free (series_name);
			g_slist_free (list);
		}
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
	char const *style_name = g_hash_table_lookup (state->so_styles, so);

	if (GNM_IS_SO_GRAPH (so))
		odf_write_graph (state, so, g_hash_table_lookup (state->graphs, so), style_name);
	else if (GNM_IS_SO_IMAGE (so)) {
		gsf_xml_out_start_element (state->xml, DRAW "frame");
		if (style_name != NULL)
			gsf_xml_out_add_cstr (state->xml, DRAW "style-name", style_name);
		odf_write_frame_size (state, so);
		odf_write_image (state, so, g_hash_table_lookup (state->images, so));
		gsf_xml_out_end_element (state->xml); /*  DRAW "frame" */
	} else {
		gsf_xml_out_start_element (state->xml, DRAW "frame");
		if (style_name != NULL)
			gsf_xml_out_add_cstr (state->xml, DRAW "style-name", style_name);
		odf_write_frame_size (state, so);
		gsf_xml_out_start_element (state->xml, DRAW "text-box");
		gsf_xml_out_simple_element (state->xml, TEXT "p",
					    "Missing Framed Sheet Object");
		gsf_xml_out_end_element (state->xml); /*  DRAW "text-box" */
		gsf_xml_out_end_element (state->xml); /*  DRAW "frame" */
	}
}

static void
custom_shape_path_collector (GOPath *path, GString *gstr)
{
	char *path_string = NULL;
	path_string = go_path_to_svg (path);
	g_string_append (gstr, " N ");
	g_string_append (gstr, path_string);
	g_free (path_string);
}

static void
odf_write_custom_shape (GnmOOExport *state, SheetObject *so)
{
	gchar const *style_name = g_hash_table_lookup (state->so_styles, so);
	gchar *text = NULL;
	PangoAttrList * markup = NULL;
	gboolean pp = TRUE;
	GOPath *path = NULL;
	GPtrArray *paths;
	char *path_string = NULL;
	char *view_box = NULL;

	g_object_get (G_OBJECT (so), "text", &text, "markup", &markup, "path", &path,
		      "paths", &paths, "viewbox", &view_box, NULL);

	gsf_xml_out_start_element (state->xml, DRAW "custom-shape");

	if (style_name != NULL)
		gsf_xml_out_add_cstr (state->xml, DRAW "style-name", style_name);
	odf_write_frame_size (state, so);

	g_object_get (G_OBJECT (state->xml), "pretty-print", &pp, NULL);
	g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
	gsf_xml_out_start_element (state->xml, TEXT "p");
	odf_new_markup (state, markup, text);
	gsf_xml_out_end_element (state->xml);   /* p */
	g_object_set (G_OBJECT (state->xml), "pretty-print", pp, NULL);

	if (path) {
		char *ps = go_path_to_svg (path);
		path_string = g_strconcat (ps, " N", NULL);
		g_free(ps);
	}
	if (paths) {
		GString *gstr = g_string_new (path_string);
		g_ptr_array_foreach (paths, (GFunc)custom_shape_path_collector, gstr);
		g_string_append (gstr, " N");
		path_string  = g_string_free (gstr, FALSE);
	}
	if (path_string) {
		gsf_xml_out_start_element (state->xml, DRAW "enhanced-geometry");
		gsf_xml_out_add_cstr (state->xml, SVG "viewBox", view_box);
		gsf_xml_out_add_cstr (state->xml, DRAW "enhanced-path", path_string);
		gsf_xml_out_end_element (state->xml); /*  DRAW "enhanced-geometry" */
	}
	gsf_xml_out_end_element (state->xml); /*  DRAW "custom-shape" */

	g_free (text);
	g_free (path_string);
	g_free (view_box);
	if (markup)
		pango_attr_list_unref (markup);
	if (paths)
		g_ptr_array_unref (paths);
	if (path)
		go_path_free (path);
}


static void
odf_write_control (GnmOOExport *state, SheetObject *so, char const *id)
{
        gchar const *style_name = g_hash_table_lookup (state->so_styles, so);

	gsf_xml_out_start_element (state->xml, DRAW "control");
	if (style_name != NULL)
		gsf_xml_out_add_cstr (state->xml, DRAW "style-name", style_name);
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
	PangoAttrList * markup = NULL;
	gchar const *style_name = g_hash_table_lookup (state->so_styles, so);
	gboolean pp = TRUE;

	g_object_get (G_OBJECT (so), "is-oval", &is_oval, "text", &text, "markup", &markup, NULL);
	element = is_oval ? DRAW "ellipse" : DRAW "rect";

	gsf_xml_out_start_element (state->xml, element);
	if (style_name != NULL)
		gsf_xml_out_add_cstr (state->xml, DRAW "style-name", style_name);
	odf_write_frame_size (state, so);

	g_object_get (G_OBJECT (state->xml), "pretty-print", &pp, NULL);
	g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
	gsf_xml_out_start_element (state->xml, TEXT "p");
	odf_new_markup (state, markup, text);
	gsf_xml_out_end_element (state->xml);   /* p */
	g_object_set (G_OBJECT (state->xml), "pretty-print", pp, NULL);

	g_free (text);
	if (markup)
		pango_attr_list_unref (markup);

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
	int z;
	char *name = NULL;

	gsf_xml_out_start_element (state->xml, DRAW "line");
	g_object_get (G_OBJECT (so), "name", &name, NULL);
	if (name) {
	        gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "name", name);
		g_free (name);
	}
	if (style_name != NULL)
		gsf_xml_out_add_cstr (state->xml, DRAW "style-name", style_name);
	z = g_slist_length (state->sheet->sheet_objects) -
		sheet_object_get_stacking (so);
	gsf_xml_out_add_int (state->xml, DRAW "z-index", z);

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

	if (anchor->mode == GNM_SO_ANCHOR_TWO_CELLS) {
		sheet_object_anchor_to_offset_pts (anchor, state->sheet, res_pts);
		odf_add_pt (state->xml, TABLE "end-x", res_pts[2]);
		odf_add_pt (state->xml, TABLE "end-y", res_pts[3]);

		gnm_cellref_init (&ref, (Sheet *) state->sheet, r->end.col, r->end.row, TRUE);
		texpr =  gnm_expr_top_new (gnm_expr_new_cellref (&ref));
		parse_pos_init_sheet (&pp, state->sheet);
		formula = gnm_expr_top_as_string (texpr, &pp, state->conv);
		gnm_expr_top_unref (texpr);
		gsf_xml_out_add_cstr (state->xml, TABLE "end-cell-address",
					  odf_strip_brackets (formula));
		g_free (formula);
	}

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
		if (GNM_IS_FILTER_COMBO (so) || GNM_IS_VALIDATION_COMBO(so))
			continue;
		if (id != NULL)
			odf_write_control (state, so, id);
		else if (GNM_IS_CELL_COMMENT (so))
			odf_write_comment (state, GNM_CELL_COMMENT (so));
		else if (GNM_IS_SO_FILLED (so))
			odf_write_so_filled (state, so);
		else if (GNM_IS_SO_LINE (so))
			odf_write_line (state, so);
		else if (GNM_IS_SO_PATH (so))
			odf_write_custom_shape (state, so);
		else
			odf_write_frame (state, so);

	}
}

static void
odf_write_link_start (GnmOOExport *state, GnmHLink *lnk)
{
	GType const t = G_OBJECT_TYPE (lnk);
	char *link_text = NULL;

	gsf_xml_out_start_element (state->xml, TEXT "a");
	gsf_xml_out_add_cstr (state->xml, XLINK "type", "simple");
	gsf_xml_out_add_cstr (state->xml, XLINK "actuate", "onRequest");

	if (g_type_is_a (t, gnm_hlink_url_get_type ())) {
		// This includes email
		link_text = g_strdup (gnm_hlink_get_target (lnk));
	} else if (g_type_is_a (t, gnm_hlink_cur_wb_get_type ())) {
		GnmExprTop const *texpr = gnm_hlink_get_target_expr (lnk);
		GnmSheetRange sr;

		if (texpr && GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_NAME) {
			GnmParsePos pp;
			char *s;
			parse_pos_init_sheet (&pp, gnm_hlink_get_sheet (lnk));
			s = gnm_expr_top_as_string (texpr, &pp, state->conv);
			link_text = g_strconcat ("#", s, NULL);
			g_free (s);
		} else if (gnm_hlink_get_range_target (lnk, &sr)) {
			link_text = g_strconcat
				("#",
				 sr.sheet->name_unquoted, ".",
				 range_as_string (&sr.range),
				 NULL);
		}
	} else {
		g_warning ("Unexpected hyperlink type");
	}

	gsf_xml_out_add_cstr (state->xml, XLINK "href", link_text ? link_text : "#");
	g_free (link_text);

	gsf_xml_out_add_cstr (state->xml, OFFICE "title", gnm_hlink_get_tip (lnk));
}

static void
odf_write_link_end (GnmOOExport *state, GnmHLink *lnk)
{
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
			GnmInputMsg *im;
			if (name != NULL)
				gsf_xml_out_add_cstr (state->xml,
						      TABLE "style-name", name);
			if (val != NULL) {
				char *vname = oo_item_name (state, OO_ITEM_VALIDATION, val);
				gsf_xml_out_add_cstr (state->xml,
						      TABLE "content-validation-name", vname);
				g_free (vname);
			} else if (NULL != (im = gnm_style_get_input_msg (style))) {
				char *vname = oo_item_name (state, OO_ITEM_INPUT_MSG, im);
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

static  gboolean
odf_cellspan_is_empty (int col, GnmCell const *ok_span_cell)
{
	Sheet *sheet = ok_span_cell->base.sheet;
	int row = ok_span_cell->pos.row;
	ColRowInfo *ri = sheet_row_get (sheet, row);
	CellSpanInfo const *span = row_span_get (ri, col);
	GnmCell const *tmp;
	GnmCellPos pos;

	if (span != NULL && span->cell != ok_span_cell)
		return FALSE;

	pos.row = row;
	pos.col = col;
	if (gnm_sheet_merge_contains_pos (sheet, &pos) != NULL)
		return FALSE;

	tmp = sheet_cell_get (sheet, col, row);

	return (tmp == NULL || tmp->value == NULL ||
		(VALUE_IS_EMPTY (tmp->value) && !gnm_cell_has_expr(tmp)));
}

static void
odf_write_cell (GnmOOExport *state, GnmCell *cell, GnmRange const *merge_range,
		GnmStyle const *style, GSList *objects)
{
	int rows_spanned = 0, cols_spanned = 0;
	GnmHLink *lnk = NULL;
	gboolean col_spanned_fake = FALSE;

	if (merge_range != NULL) {
		rows_spanned = merge_range->end.row - merge_range->start.row + 1;
		cols_spanned = merge_range->end.col - merge_range->start.col + 1;
	}

	if (style && cell && cols_spanned <= 1 && gnm_style_get_align_h (style) == GNM_HALIGN_CENTER_ACROSS_SELECTION) {
		/* We have to simulate GNM_HALIGN_CENTER_ACROSS_SELECTION by a merge */
		int cell_col = cell->pos.col;
		int cell_row = cell->pos.row;
		int max_col_spanned = gnm_sheet_get_max_cols (state->sheet) - cell_col;
		cols_spanned = 1;
		while (cols_spanned < max_col_spanned) {
			ColRowInfo const *ci;
			cell_col++;
			ci = sheet_col_get_info (state->sheet, cell_col);
			if (ci->visible) {
				if (odf_cellspan_is_empty (cell_col, cell)) {
					GnmStyle const * const cstyle =
						sheet_style_get (state->sheet, cell_col, cell_row);
					if (gnm_style_get_align_h (cstyle) != GNM_HALIGN_CENTER_ACROSS_SELECTION)
						break;
				} else
					break;
			}
			cols_spanned++;
		}
		col_spanned_fake = (cols_spanned > 1);
	}

	gsf_xml_out_start_element (state->xml, TABLE "table-cell");

	if (cols_spanned > 1) {
		gsf_xml_out_add_int (state->xml,
				     TABLE "number-columns-spanned", cols_spanned);
		if (col_spanned_fake && state->with_extension)
			odf_add_bool (state->xml, GNMSTYLE "columns-spanned-fake", TRUE);
	}
	if (rows_spanned > 1)
		gsf_xml_out_add_int (state->xml,
				     TABLE "number-rows-spanned", rows_spanned);
	if (style) {
		char const * name = odf_find_style (state, style);
		GnmValidation const *val = gnm_style_get_validation (style);
		if (name != NULL)
			gsf_xml_out_add_cstr (state->xml,
					      TABLE "style-name", name);
		if (val != NULL) {
			char *vname = oo_item_name (state, OO_ITEM_VALIDATION, val);
			gsf_xml_out_add_cstr (state->xml,
					      TABLE "content-validation-name", vname);
			g_free (vname);
		}
		lnk = gnm_style_get_hlink (style);
	}

	if (cell != NULL) {
		if ((NULL != cell->base.texpr) &&
		    !gnm_expr_top_is_array_elem (cell->base.texpr, NULL, NULL)) {
			char *formula, *eq_formula;
			GnmParsePos pp;

			if (gnm_cell_is_array (cell)) {
				if (gnm_expr_top_is_array_corner (cell->base.texpr)) {
					int cols, rows;

					gnm_expr_top_get_array_size (cell->base.texpr, &cols, &rows);
					gsf_xml_out_add_uint (state->xml,
							      TABLE "number-matrix-columns-spanned",
							      (unsigned int)cols);
					gsf_xml_out_add_uint (state->xml,
							      TABLE "number-matrix-rows-spanned",
							      (unsigned int)rows);
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

		switch (cell->value->v_any.type) {
		case VALUE_EMPTY:
			break;
		case VALUE_BOOLEAN:
			gsf_xml_out_add_cstr_unchecked (state->xml,
							OFFICE "value-type", "boolean");
			odf_add_bool (state->xml, OFFICE "boolean-value",
				value_get_as_bool (cell->value, NULL));
			break;
		case VALUE_FLOAT: {
			GOFormat const *fmt = gnm_cell_get_format_given_style (cell, style);
			if (go_format_is_date (fmt)) {
				char *str;
				gnm_float f = value_get_as_float (cell->value);
				if (f == gnm_floor (f)) {
					gsf_xml_out_add_cstr_unchecked (state->xml,
									OFFICE "value-type", "date");
					str = format_value (state->date_fmt, cell->value, -1, workbook_date_conv (state->wb));
					gsf_xml_out_add_cstr (state->xml, OFFICE "date-value", str);
				} else {
					gsf_xml_out_add_cstr_unchecked (state->xml,
									OFFICE "value-type", "date");
					str = format_value (state->date_long_fmt, cell->value, -1, workbook_date_conv (state->wb));
					gsf_xml_out_add_cstr (state->xml, OFFICE "date-value", str);
				}
				g_free (str);
			} else if (go_format_is_time (fmt) && (value_get_as_float (cell->value) >= 0)) {
				char *str;
				gsf_xml_out_add_cstr_unchecked (state->xml,
								OFFICE "value-type", "time");
				str = format_value (state->time_fmt, cell->value, -1, workbook_date_conv (state->wb));
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
				/* error literals.                                       */
				char const *cv = value_peek_string (cell->value);
				char *eq_formula = g_strdup_printf ("of:=%s", cv);

				if (state->with_extension)
					gsf_xml_out_add_cstr (state->xml, GNMSTYLE "error-value", cv);

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
			/* If this is a non-formula cell we show only the real formatted content */
			/* If the alignmnet type is 'FILL' we do need to give the string value!  */
			if (NULL != cell->base.texpr || gnm_style_get_align_h (style) == GNM_HALIGN_FILL)
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
		gboolean pprint = TRUE;
		g_object_get (G_OBJECT (state->xml), "pretty-print", &pprint, NULL);
		g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);

		if ((VALUE_FMT (cell->value) == NULL)
		    || (!VALUE_IS_STRING (cell->value))
		    || (!go_format_is_markup (VALUE_FMT (cell->value)))) {
			char *rendered_string = gnm_cell_get_rendered_text (cell);
			gboolean white_written = TRUE;

			gsf_xml_out_start_element (state->xml, TEXT "p");
			if (lnk) odf_write_link_start (state, lnk);
			if (*rendered_string != '\0')
				odf_add_chars (state, rendered_string, strlen (rendered_string),
					       &white_written);
			if (lnk) odf_write_link_end (state, lnk);
			gsf_xml_out_end_element (state->xml);   /* p */
			g_free (rendered_string);
		} else {
			GString *str = g_string_new (NULL);
			const PangoAttrList * markup;

			value_get_as_gstring (cell->value, str, state->conv);
			markup = go_format_get_markup (VALUE_FMT (cell->value));

			gsf_xml_out_start_element (state->xml, TEXT "p");
			if (lnk) odf_write_link_start (state, lnk);
			odf_new_markup (state, markup, str->str);
			if (lnk) odf_write_link_end (state, lnk);
			gsf_xml_out_end_element (state->xml);   /* p */

			g_string_free (str, TRUE);
		}
		g_object_set (G_OBJECT (state->xml), "pretty-print", pprint, NULL);
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

	if (ci != NULL && !ci->visible)
		gsf_xml_out_add_cstr (state->xml, TABLE "visibility", ci->in_filter ? "filter" : "collapse");
}

static void
odf_write_formatted_columns (GnmOOExport *state, Sheet const *sheet,
			     GPtrArray *col_styles, int from, int to)
{
	int number_cols_rep;
	ColRowInfo const *last_ci;
	GnmStyle *last_col_style = NULL;
	int i;

	gsf_xml_out_start_element (state->xml, TABLE "table-column");
	number_cols_rep = 1;
	last_col_style = filter_style (state->default_style_region->style,
				       g_ptr_array_index (col_styles, 0));
	last_ci = sheet_col_get (sheet, 0);
	write_col_style (state, last_col_style, last_ci, sheet);

	for (i = from+1; i < to; i++) {
		GnmStyle *this_col_style = filter_style (state->default_style_region->style,
							 g_ptr_array_index (col_styles, i));
		ColRowInfo const *this_ci = sheet_col_get (sheet, i);

		if ((this_col_style == last_col_style) && col_row_info_equal (last_ci, this_ci))
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

	if (ci != NULL && !ci->visible)
		gsf_xml_out_add_cstr (state->xml, TABLE "visibility", ci->in_filter ? "filter" : "collapse");
}

static gboolean
row_info_equal (GnmOOExport *state, Sheet const *sheet,
		ColRowInfo const *ci1, ColRowInfo const *ci2)
{
	if (ci1 == ci2)
		return TRUE;
	else {
		char const *n1 =
			odf_find_row_style (state,
					    (ci1 == NULL) ? &sheet->rows.default_style: ci1,
					    FALSE);
		char const *n2 =
			odf_find_row_style (state,
					    (ci2 == NULL) ? &sheet->rows.default_style: ci2,
					    FALSE);
		return g_str_equal (n1, n2);
	}
}

static gboolean
compare_row_styles (const Sheet *sheet, GnmStyle **styles, int orow)
{
	GnmStyle **ostyles = sheet_style_get_row2 (sheet, orow);
	gboolean res;

	res = !memcmp (styles, ostyles,
		       gnm_sheet_get_max_cols (sheet) * sizeof (GnmStyle *));

	g_free (ostyles);

	return res;
}

static GSList *
odf_sheet_objects_get (Sheet const *sheet, GnmCellPos const *pos)
{
	GSList *res = NULL;
	GSList *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next ) {
		SheetObject *so = GNM_SO (ptr->data);
		SheetObjectAnchor const *anchor = sheet_object_get_anchor (so);
		if (anchor->mode == GNM_SO_ANCHOR_ABSOLUTE) {
			if (pos == NULL)
				res = g_slist_prepend (res, so);
		} else if (pos && gnm_cellpos_equal (&anchor->cell_bound.start, pos))
			res = g_slist_prepend (res, so);
	}
	return res;
}

enum {
	RF_CELL = 1,
	RF_PAGEBREAK = 2,
	RF_OBJECT = 4,
	RF_STYLE = 8
};

static void
odf_write_content_rows (GnmOOExport *state, Sheet const *sheet,
			int from, int to,
			int row_length,	GSList **sheet_merges,
			GnmPageBreaks *pb, GPtrArray *col_styles)
{
	int row;
	GPtrArray *all_cells;
	guint cno = 0;
	guint8 *row_flags;

	row_flags = g_new0 (guint8, gnm_sheet_get_max_rows (sheet));

	/* Find out what rows have objects.  */
	{
		GSList *ptr;

		for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next ) {
			SheetObject *so = GNM_SO (ptr->data);
			SheetObjectAnchor const *anchor = sheet_object_get_anchor (so);
			int row = anchor->cell_bound.start.row;
			row_flags[row] |= RF_OBJECT;
		}
	}

	/* Find out what rows have page breaks.  */
	for (row = from; row < to; row++) {
		if (gnm_page_breaks_get_break (pb, row) != GNM_PAGE_BREAK_NONE)
			row_flags[row] |= RF_PAGEBREAK;
	}

	/* Find out what rows have cells.  */
	{
		GnmRange fake_extent;
		unsigned ui;
		range_init_rows (&fake_extent, sheet, from, to - 1);
		all_cells = sheet_cells ((Sheet*)sheet, &fake_extent);
		for (ui = 0; ui < all_cells->len; ui++) {
			GnmCell *cell = g_ptr_array_index (all_cells, ui);
			row_flags[cell->pos.row] |= RF_CELL;
		}
		/* Add a NULL to simplify code.  */
		g_ptr_array_add (all_cells, NULL);
	}

	/* Find out what rows have style not covered by column styles.  */
	{
		GByteArray *non_defaults_rows =
			sheet_style_get_nondefault_rows (sheet, col_styles);
		for (row = from; row < to; row++)
			if (non_defaults_rows->data[row])
				row_flags[row] |= RF_STYLE;
		g_byte_array_free (non_defaults_rows, TRUE);
	}

	for (row = from; row < to; /* nothing here */) {
		ColRowInfo const *ci = sheet_row_get (sheet, row);
		GnmStyle const *null_style = NULL;
		int null_cell = 0;
		int covered_cell = 0;
		GnmCellPos pos;
		int repeat_count = 1;
		guint8 rf = row_flags[row];
		GnmStyle **row_styles =	(rf & RF_STYLE)
			? sheet_style_get_row2 (sheet, row)
			: NULL;

		pos.row = row;

		if (rf & RF_PAGEBREAK)
			gsf_xml_out_simple_element (state->xml,
						    TEXT "soft-page-break",
						    NULL);

		gsf_xml_out_start_element (state->xml, TABLE "table-row");
		write_row_style (state, ci, sheet);

		if ((rf & ~RF_STYLE) == 0) {
			/*
			 * We have nothing but style (possibly default) in this
			 * row, so see if some rows following this one are
			 * identical.
			 */
			int row2;
			while ((row2 = row + repeat_count) < to &&
			       row_flags[row2] == rf &&
			       row_info_equal (state, sheet, ci, sheet_row_get (sheet, row2)) &&
			       (rf == 0 || compare_row_styles (sheet, row_styles, row2)))
				repeat_count++;

			if (repeat_count > 1)
				gsf_xml_out_add_int (state->xml, TABLE "number-rows-repeated",
						     repeat_count);
		}

		if (rf) {
			int col;

			for (col = 0; col < row_length; col++) {
				GnmCell *current_cell;
				GnmRange const	*merge_range;
				GSList *objects;
				GnmStyle const *this_style = row_styles
					? row_styles[col]
					: g_ptr_array_index (col_styles, col);

				current_cell = g_ptr_array_index (all_cells, cno);
				if (current_cell &&
				    current_cell->pos.row == row &&
				    current_cell->pos.col == col)
					cno++;
				else
					current_cell = NULL;

				pos.col = col;

				merge_range = gnm_sheet_merge_is_corner (sheet, &pos);

				if (odf_cell_is_covered (sheet, current_cell, col, row,
							 merge_range, sheet_merges)) {
					odf_write_empty_cell (state, null_cell, null_style, NULL);
					null_cell = 0;
					covered_cell++;
					continue;
				}

				objects = (rf & RF_OBJECT)
					? odf_sheet_objects_get (sheet, &pos)
					: NULL;

				if ((!(current_cell && gnm_cell_has_expr(current_cell))) &&
				    (merge_range == NULL) && (objects == NULL) &&
				    gnm_cell_is_empty (current_cell) &&
				    !gnm_style_get_hlink (this_style)) {
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
				odf_write_cell (state, current_cell, merge_range, this_style, objects);

				g_slist_free (objects);

			}
		} else
			null_cell = row_length;

		odf_write_empty_cell (state, null_cell, null_style, NULL);
		null_cell = 0;
		if (covered_cell > 0)
			odf_write_covered_cell (state, &covered_cell);

		gsf_xml_out_end_element (state->xml);   /* table-row */

		row += repeat_count;
		g_free (row_styles);
	}

	g_ptr_array_free (all_cells, TRUE);
	g_free (row_flags);
}

static void
odf_write_sheet (GnmOOExport *state)
{
	/* While ODF allows the TABLE "table-columns" wrapper, */
	/* and TABLE "table-rows" wrapper, */
	/* MS Excel 2010 stumbles over it */
	/* So we may not use them! */

	Sheet const *sheet = state->sheet;
	int max_cols = gnm_sheet_get_max_cols (sheet);
	int max_rows = gnm_sheet_get_max_rows (sheet);
	GPtrArray *col_styles;
	GnmRange r;
	GSList *sheet_merges = NULL;
	GnmPageBreaks *pb = sheet->print_info->page_breaks.v;

	col_styles = sheet_style_most_common (sheet, TRUE);

	/* ODF does not allow us to mark soft page breaks between columns */
	if (print_load_repeat_range (sheet->print_info->repeat_left, &r, sheet)) {
		int repeat_left_start, repeat_left_end;
		repeat_left_start = r.start.col;
		repeat_left_end   = r.end.col;

		if (repeat_left_start > 0)
			odf_write_formatted_columns (state, sheet, col_styles,
						     0, repeat_left_start);
		gsf_xml_out_start_element
			(state->xml, TABLE "table-header-columns");
		odf_write_formatted_columns (state, sheet, col_styles,
					     repeat_left_start,
					     repeat_left_end + 1);
		gsf_xml_out_end_element (state->xml);
		if (repeat_left_end < max_cols)
			odf_write_formatted_columns (state, sheet, col_styles,
						     repeat_left_end + 1, max_cols);
	} else
		odf_write_formatted_columns (state, sheet, col_styles, 0, max_cols);

	if (print_load_repeat_range (sheet->print_info->repeat_top, &r, sheet)) {
		int repeat_top_start, repeat_top_end;
		repeat_top_start = r.start.row;
		repeat_top_end   = r.end.row;
		if (repeat_top_start > 0)
			odf_write_content_rows (state, sheet,
						0, repeat_top_start,
						max_cols, &sheet_merges, pb, col_styles);
		gsf_xml_out_start_element
			(state->xml, TABLE "table-header-rows");
		odf_write_content_rows (state, sheet,
					repeat_top_start, repeat_top_end + 1,
					max_cols, &sheet_merges, pb, col_styles);
		gsf_xml_out_end_element (state->xml);
		if (repeat_top_end < max_rows)
			odf_write_content_rows (state, sheet,
						repeat_top_end + 1, max_rows,
						max_cols, &sheet_merges, pb, col_styles);
	} else
		odf_write_content_rows (state, sheet,
					0, max_rows,
					max_cols, &sheet_merges, pb, col_styles);

	g_slist_free_full (sheet_merges, g_free);
	g_ptr_array_free (col_styles, TRUE);

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
		char *lnk = NULL;
		GnmParsePos pp;

		parse_pos_init_sheet (&pp, state->sheet);
		lnk = gnm_expr_top_as_string (texpr, &pp, state->conv);

		if (state->odf_version > 101)
			gsf_xml_out_add_cstr (state->xml,
					      FORM "source-cell-range",
					      odf_strip_brackets (lnk));
		else
			gsf_xml_out_add_cstr (state->xml,
					      GNMSTYLE "source-cell-range",
					      odf_strip_brackets (lnk));
		g_free (lnk);
		gnm_expr_top_unref (texpr);
	}
}

static void
odf_write_sheet_control_linked_cell (GnmOOExport *state, GnmExprTop const *texpr)
{
	if (texpr && gnm_expr_top_is_rangeref (texpr)) {
		char *lnk = NULL;
		GnmParsePos pp;

		parse_pos_init_sheet (&pp, state->sheet);
		lnk = gnm_expr_top_as_string (texpr, &pp, state->conv);

		if (state->odf_version > 101)
			gsf_xml_out_add_cstr (state->xml, FORM "linked-cell",
					      odf_strip_brackets (lnk));
		else
			gsf_xml_out_add_cstr (state->xml, GNMSTYLE "linked-cell",
					      odf_strip_brackets (lnk));
		g_free (lnk);
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
	go_xml_out_add_double (state->xml, FORM "value", gtk_adjustment_get_value (adj));
	go_xml_out_add_double (state->xml, FORM "min-value", gtk_adjustment_get_lower (adj));
	go_xml_out_add_double (state->xml, FORM "max-value", gtk_adjustment_get_upper (adj));
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
	gboolean active = FALSE;

	g_object_get (G_OBJECT (so), "text", &label, "active", &active, NULL);

	odf_sheet_control_start_element (state, so, FORM "checkbox");

	gsf_xml_out_add_cstr (state->xml, FORM "label", label);
	gsf_xml_out_add_cstr (state->xml, FORM "current-state", active ? "checked" : "unchecked");

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
			      char const *element, gboolean is_listbox)
{
	GnmExprTop const *texpr = sheet_widget_list_base_get_result_link (so);
	gboolean as_index = sheet_widget_list_base_result_type_is_index (so);

	odf_sheet_control_start_element (state, so, element);

	odf_write_sheet_control_linked_cell (state, texpr);

	texpr = sheet_widget_list_base_get_content_link (so);
	odf_write_sheet_control_content (state, texpr);

	if (state->odf_version > 101 && is_listbox)
		gsf_xml_out_add_cstr_unchecked
			(state->xml, FORM "list-linkage-type",
			 as_index ? "selection-indices" : "selection");
	else if (state->with_extension)
		gsf_xml_out_add_cstr_unchecked
			(state->xml, GNMSTYLE "list-linkage-type",
			 as_index ? "selection-indices" : "selection");
	if (is_listbox)
		gsf_xml_out_add_int (state->xml, FORM "bound-column", 1);
	gsf_xml_out_end_element (state->xml);
}

static void
odf_write_sheet_control_radio_button (GnmOOExport *state, SheetObject *so)
{
	GnmExprTop const *texpr = sheet_widget_radio_button_get_link (so);
	GnmValue const *val = sheet_widget_radio_button_get_value (so);
	char *label = NULL;
	gboolean active = FALSE;

	g_object_get (G_OBJECT (so), "text", &label, "active", &active, NULL);

	odf_sheet_control_start_element (state, so, FORM "radio");

	gsf_xml_out_add_cstr (state->xml, FORM "label", label);
	odf_add_bool (state->xml, FORM "current-selected", active);

	if (val != NULL) {
		switch (val->v_any.type) {
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

	odf_sheet_control_start_element (state, so, FORM "button");

	g_object_get (G_OBJECT (so), "text", &label, NULL);
	gsf_xml_out_add_cstr (state->xml, FORM "label", label);
	g_free (label);

	gsf_xml_out_add_cstr_unchecked (state->xml, FORM "button-type", "push");

	if (texpr != NULL ) {
		char *lnk = NULL, *name = NULL;
		GnmParsePos pp;

		parse_pos_init_sheet (&pp, state->sheet);
		lnk = gnm_expr_top_as_string (texpr, &pp, state->conv);

		gsf_xml_out_start_element (state->xml, OFFICE "event-listeners");

		gsf_xml_out_start_element (state->xml, SCRIPT "event-listener");
		gsf_xml_out_add_cstr_unchecked (state->xml, SCRIPT "event-name",
						"dom:mousedown");
		gsf_xml_out_add_cstr_unchecked (state->xml, SCRIPT "language",
						GNMSTYLE "short-macro");
		name = g_strdup_printf ("set-to-TRUE:%s", odf_strip_brackets (lnk));
		gsf_xml_out_add_cstr (state->xml, SCRIPT "macro-name", name);
		g_free (name);
		gsf_xml_out_end_element (state->xml); /* script:event-listener */

		gsf_xml_out_start_element (state->xml, SCRIPT "event-listener");
		gsf_xml_out_add_cstr_unchecked (state->xml, SCRIPT "event-name",
						"dom:mouseup");
		gsf_xml_out_add_cstr_unchecked (state->xml, SCRIPT "language",
						GNMSTYLE "short-macro");
		name = g_strdup_printf ("set-to-FALSE:%s", odf_strip_brackets (lnk));
		gsf_xml_out_add_cstr (state->xml, SCRIPT "macro-name", name);
		g_free (name);
		gsf_xml_out_end_element (state->xml); /* script:event-listener */

		gsf_xml_out_end_element (state->xml); /* office:event-listeners */

		g_free (lnk);
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
						      FORM "listbox", TRUE);
		else if (GNM_IS_SOW_COMBO (so))
			odf_write_sheet_control_list (state, so,
						      FORM "combobox", FALSE);
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
		if (filter->fields->len > 1) {
			gsf_xml_out_start_element (state->xml, TABLE "filter-and");
			for (i = 0 ; i < filter->fields->len ; i++)
				odf_write_filter_cond (state, filter, i);
			gsf_xml_out_end_element (state->xml); /* </table:filter-and> */
		} else if (filter->fields->len == 1)
			odf_write_filter_cond (state, filter, 0);
		gsf_xml_out_end_element (state->xml); /* </table:filter> */
	}

	gsf_xml_out_end_element (state->xml); /* </table:database-range> */
}

static void
odf_validation_general_attributes (GnmOOExport *state, GnmValidation const *val)
{
	odf_add_bool (state->xml,  TABLE "allow-empty-cell", val->allow_blank);
	gsf_xml_out_add_cstr (state->xml,  TABLE "display-list",
			      val->use_dropdown ? "unsorted" : "none");
}

static void
odf_validation_base_cell_address (GnmOOExport *state,
				  Sheet *sheet, GnmStyleRegion const *sr,
				  GnmParsePos *pp)
{
	GnmExprTop const *texpr;
	char *formula;
	GnmCellRef ref;

	gnm_cellref_init (&ref, sheet,
			  sr->range.start.col,
			  sr->range.start.row, TRUE);
	texpr =  gnm_expr_top_new (gnm_expr_new_cellref (&ref));
	parse_pos_init (pp, (Workbook *)state->wb, sheet,
			sr->range.start.col,
			sr->range.start.row);
	formula = gnm_expr_top_as_string (texpr, pp, state->conv);
	gsf_xml_out_add_cstr (state->xml, TABLE "base-cell-address",
			      odf_strip_brackets (formula));
	g_free (formula);
	gnm_expr_top_unref (texpr);
}

static void
odf_validation_append_expression (GnmOOExport *state, GString *str, GnmExprTop const *texpr,
				  GnmParsePos *pp)
{
	char *formula;

	formula = gnm_expr_top_as_string (texpr, pp, state->conv);
	g_string_append (str, formula);
	g_free (formula);
}

static void
odf_validation_append_expression_pair (GnmOOExport *state, GString *str,
				       GnmValidation const *val,
				       GnmParsePos *pp)
{
	g_string_append_c (str, '(');
	odf_validation_append_expression (state, str,
					  val->deps[0].base.texpr, pp);
	g_string_append_c (str, ',');
	odf_validation_append_expression (state, str,
					  val->deps[1].base.texpr, pp);
	g_string_append_c (str, ')');
}


static void
odf_validation_general (GnmOOExport *state, GnmValidation const *val,
			G_GNUC_UNUSED Sheet *sheet,
			G_GNUC_UNUSED GnmStyleRegion const *sr,
			char const *prefix, GnmParsePos *pp)
{
	GString *str = g_string_new ("of:");
	GnmExprTop const *texpr0 = val->deps[0].base.texpr;

	g_string_append (str, prefix);

	switch (val->op) {
	case GNM_VALIDATION_OP_NONE:
		g_string_append (str, "is-true-formula(1)");
		break;
	case GNM_VALIDATION_OP_BETWEEN:
		g_string_append (str, "cell-content-is-between");
		odf_validation_append_expression_pair (state, str, val, pp);
		break;
	case GNM_VALIDATION_OP_NOT_BETWEEN:
		g_string_append (str, "cell-content-is-not-between");
		odf_validation_append_expression_pair (state, str, val, pp);
		break;
	case GNM_VALIDATION_OP_EQUAL:
		g_string_append (str, "cell-content() = ");
		odf_validation_append_expression (state, str, texpr0, pp);
		break;
	case GNM_VALIDATION_OP_NOT_EQUAL:
		g_string_append (str, "cell-content() != ");
		odf_validation_append_expression (state, str, texpr0, pp);
		break;
	case GNM_VALIDATION_OP_GT:
		g_string_append (str, "cell-content() > ");
		odf_validation_append_expression (state, str, texpr0, pp);
		break;
	case GNM_VALIDATION_OP_LT:
		g_string_append (str, "cell-content() < ");
		odf_validation_append_expression (state, str, texpr0, pp);
		break;
	case GNM_VALIDATION_OP_GTE:
		g_string_append (str, "cell-content() >= ");
		odf_validation_append_expression (state, str, texpr0, pp);
		break;
	case GNM_VALIDATION_OP_LTE:
		g_string_append (str, "cell-content() <= ");
		odf_validation_append_expression (state, str, texpr0, pp);
		break;
	}

	gsf_xml_out_add_cstr (state->xml, TABLE "condition", str->str);
	g_string_free (str, TRUE);
}

static void
odf_validation_length (GnmOOExport *state, GnmValidation const *val,
		       G_GNUC_UNUSED Sheet *sheet,
		       G_GNUC_UNUSED GnmStyleRegion const *sr, GnmParsePos *pp)
{
	GString *str = g_string_new ("of:");
	GnmExprTop const *texpr0 = val->deps[0].base.texpr;

	switch (val->op) {
	case GNM_VALIDATION_OP_NONE:
		g_string_append (str, "is-true-formula(1)");
		break;
	case GNM_VALIDATION_OP_BETWEEN:
		g_string_append (str, "cell-content-text-length-is-between");
		odf_validation_append_expression_pair (state, str, val, pp);
		break;
	case GNM_VALIDATION_OP_NOT_BETWEEN:
		g_string_append (str, "cell-content-text-length-is-not-between");
		odf_validation_append_expression_pair (state, str, val, pp);
		break;
	case GNM_VALIDATION_OP_EQUAL:
		g_string_append (str, "cell-content-text-length() = ");
		odf_validation_append_expression (state, str, texpr0, pp);
		break;
	case GNM_VALIDATION_OP_NOT_EQUAL:
		g_string_append (str, "cell-content-text-length() != ");
		odf_validation_append_expression (state, str, texpr0, pp);
		break;
	case GNM_VALIDATION_OP_GT:
		g_string_append (str, "cell-content-text-length() > ");
		odf_validation_append_expression (state, str, texpr0, pp);
		break;
	case GNM_VALIDATION_OP_LT:
		g_string_append (str, "cell-content-text-length() < ");
		odf_validation_append_expression (state, str, texpr0, pp);
		break;
	case GNM_VALIDATION_OP_GTE:
		g_string_append (str, "of:cell-content-text-length() >= ");
		odf_validation_append_expression (state, str, texpr0, pp);
		break;
	case GNM_VALIDATION_OP_LTE:
		g_string_append (str, "cell-content-text-length() <= ");
		odf_validation_append_expression (state, str, texpr0, pp);
		break;
	}

	gsf_xml_out_add_cstr (state->xml, TABLE "condition", str->str);
	g_string_free (str, TRUE);
}

static void
odf_validation_custom (GnmOOExport *state, GnmValidation const *val,
		       G_GNUC_UNUSED Sheet *sheet,
		       G_GNUC_UNUSED GnmStyleRegion const *sr, GnmParsePos *pp)
{
	GString *str = g_string_new (NULL);

	g_string_append (str, "of:is-true-formula(");
	odf_validation_append_expression (state, str, val->deps[0].base.texpr, pp);
	g_string_append_c (str, ')');

	gsf_xml_out_add_cstr (state->xml, TABLE "condition", str->str);
	g_string_free (str, TRUE);
}

static void
odf_validation_in_list (GnmOOExport *state, GnmValidation const *val,
			G_GNUC_UNUSED Sheet *sheet,
			G_GNUC_UNUSED GnmStyleRegion const *sr, GnmParsePos *pp)
{
	GString *str;

	str = g_string_new ("of:cell-content-is-in-list(");
	odf_validation_append_expression (state, str, val->deps[0].base.texpr, pp);
	g_string_append_c (str, ')');

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
			GnmInputMsg const *msg = gnm_style_get_input_msg (sr->style);
			GnmParsePos pp;
			char const *message_type = NULL;
			char *name;

			if (val == NULL && msg == NULL) {
				g_warning ("Encountered NULL validation with NULL message!");
				continue;
			}

			if (!element_written) {
				gsf_xml_out_start_element
					(state->xml, TABLE "content-validations");
				element_written = TRUE;
			}
			gsf_xml_out_start_element (state->xml,
						   TABLE "content-validation");

			name = val
				? oo_item_name (state, OO_ITEM_VALIDATION, val)
				: oo_item_name (state, OO_ITEM_INPUT_MSG, msg);
			gsf_xml_out_add_cstr (state->xml, TABLE "name", name);
			g_free (name);

			if (val) {
				odf_validation_general_attributes (state, val);
				odf_validation_base_cell_address (state, sheet, sr, &pp);
				switch (val->type) {
				case GNM_VALIDATION_TYPE_ANY:
					odf_validation_general (state, val, sheet, sr, "", &pp);
					break;
				case GNM_VALIDATION_TYPE_AS_INT:
					odf_validation_general (state, val, sheet, sr,
								"cell-content-is-whole-number() and ", &pp);
					break;
				case GNM_VALIDATION_TYPE_AS_NUMBER:
					odf_validation_general (state, val, sheet, sr,
								"cell-content-is-decimal-number() and ", &pp);
					break;
				case GNM_VALIDATION_TYPE_AS_DATE:
					odf_validation_general (state, val, sheet, sr,
								"cell-content-is-date() and ", &pp);
					break;
				case GNM_VALIDATION_TYPE_AS_TIME:
					odf_validation_general (state, val, sheet, sr,
								"cell-content-is-time() and ", &pp);
					break;
				case GNM_VALIDATION_TYPE_IN_LIST:
					odf_validation_in_list (state, val, sheet, sr, &pp);
					break;
				case GNM_VALIDATION_TYPE_TEXT_LENGTH:
					odf_validation_length (state, val, sheet, sr, &pp);
					break;
				case GNM_VALIDATION_TYPE_CUSTOM:
					odf_validation_custom (state, val, sheet, sr, &pp);
					break;
				}
			}

			/* writing help message */
			if (msg) {
				char const  * msg_content = gnm_input_msg_get_msg (msg);
				char const  * msg_title = gnm_input_msg_get_title (msg);

				if (msg_content != NULL || msg_title != NULL) {
					gsf_xml_out_start_element (state->xml,
								   TABLE "help-message");
					odf_add_bool (state->xml, TABLE "display", TRUE);
					if (msg_title != NULL)
						gsf_xml_out_add_cstr (state->xml, TABLE "title", msg_title);

					if (msg_content != NULL && strlen (msg_content) > 0) {
						gboolean white_written = TRUE;
						gboolean pp = TRUE;
						g_object_get (G_OBJECT (state->xml), "pretty-print", &pp, NULL);
						g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
						gsf_xml_out_start_element (state->xml, TEXT "p");
						odf_add_chars (state, msg_content, strlen (msg_content),
							       &white_written);
						gsf_xml_out_end_element (state->xml);   /* p */
						g_object_set (G_OBJECT (state->xml), "pretty-print", pp, NULL);
					}

					gsf_xml_out_end_element (state->xml);
					/* help message written */
				}
			}

			if (val) {
				/* writing error message */
				gsf_xml_out_start_element (state->xml,
							   TABLE "error-message");
				odf_add_bool (state->xml, TABLE "display", TRUE);
				switch (val->style) {
				case GNM_VALIDATION_STYLE_NONE:
				case GNM_VALIDATION_STYLE_INFO:
				case GNM_VALIDATION_STYLE_PARSE_ERROR:
					message_type = "information";
					break;
				case GNM_VALIDATION_STYLE_STOP:
					message_type = "stop";
					break;
				case GNM_VALIDATION_STYLE_WARNING:
					message_type = "warning";
					break;
				}
				gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "message-type", message_type);
				if (val->title != NULL)
					gsf_xml_out_add_cstr (state->xml, TABLE "title", val->title->str);

				if (val->msg != NULL && go_string_get_len (val->msg) > 0) {
					gboolean white_written = TRUE;
					gboolean pp = TRUE;
					g_object_get (G_OBJECT (state->xml), "pretty-print", &pp, NULL);
					g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
					gsf_xml_out_start_element (state->xml, TEXT "p");
					odf_add_chars (state, val->msg->str, go_string_get_len (val->msg), &white_written);
					gsf_xml_out_end_element (state->xml);   /* p */
					g_object_set (G_OBJECT (state->xml), "pretty-print", pp, NULL);
				}

				gsf_xml_out_end_element (state->xml);
				/* error message written */
			}

			gsf_xml_out_end_element (state->xml);
			/* </table:content-validation> */
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
	if (state->odf_version > 101)
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
	gnm_xml_out_add_gnm_float (state->xml, TABLE "maximum-difference",
				   state->wb->iteration.tolerance);
	gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "status",
					state->wb->iteration.enabled ?  "enable" : "disable");
	gsf_xml_out_add_int (state->xml, TABLE "steps", state->wb->iteration.max_number);
	gsf_xml_out_end_element (state->xml); /* </table:iteration> */
	gsf_xml_out_end_element (state->xml); /* </table:calculation-settings> */

	odf_print_spreadsheet_content_validations (state);
}

static void
odf_write_named_expression (G_GNUC_UNUSED gpointer key, GnmNamedExpr *nexpr,
			    GnmOOExport *state)
{
	char const *name;
	gboolean is_range;
	char *formula;
	GnmCellRef ref;
	GnmExprTop const *texpr;
	Sheet *sheet;

	g_return_if_fail (nexpr != NULL);

	if (!expr_name_is_active (nexpr))
		return;

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

#if 0
		// This would be the right thing to do per the spec, but
		// Excel ignores any name that has the attribute.  LO does
		// not seem to write this.
		gsf_xml_out_add_cstr_unchecked
			(state->xml, TABLE "range-usable-as",
			 "print-range filter repeat-row repeat-column");
#endif

		if (nexpr->pos.sheet != NULL && state->with_extension
		    && (state->odf_version < 102))
			gsf_xml_out_add_cstr (state->xml, GNMSTYLE "scope",
					      nexpr->pos.sheet->name_unquoted);

		gsf_xml_out_end_element (state->xml); /* </table:named-range> */
	} else if (!expr_name_is_placeholder (nexpr) && nexpr->texpr != NULL) {
		gsf_xml_out_start_element
			(state->xml, TABLE "named-expression");
		gsf_xml_out_add_cstr (state->xml, TABLE "name", name);

		formula = gnm_expr_top_as_string (nexpr->texpr,
						  &nexpr->pos,
						  state->conv);
		if (state->odf_version > 101) {
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
		    && (state->odf_version < 102))
			gsf_xml_out_add_cstr (state->xml, GNMSTYLE "scope",
					      nexpr->pos.sheet->name_unquoted);

		gsf_xml_out_end_element (state->xml); /* </table:named-expression> */
	}
}

static GsfXMLOut *
create_new_xml_child (G_GNUC_UNUSED GnmOOExport *state, GsfOutput *child)
{
	return g_object_new (GSF_ODF_OUT_TYPE,
			     "sink", child,
			     "odf-version", state->odf_version,
			     NULL);
}

static void
odf_write_content (GnmOOExport *state, GsfOutput *child)
{
	int i;
	int graph_n = 1;
	int image_n = 1;
	gboolean has_autofilters = FALSE;
	GSList *objects;

	state->xml = create_new_xml_child (state, child);
	gsf_xml_out_set_doc_type (state->xml, "\n");
	gsf_xml_out_start_element (state->xml, OFFICE "document-content");

	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version",
					state->odf_version_string);

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

		graphs = sheet_objects_get (sheet, NULL, GNM_SO_GRAPH_TYPE);
		for (l = graphs; l != NULL; l = l->next)
			g_hash_table_insert (state->graphs, l->data,
					     g_strdup_printf ("Graph%i", graph_n++));
		g_slist_free (graphs);

		images = sheet_objects_get (sheet, NULL, GNM_SO_IMAGE_TYPE);
		for (l = images; l != NULL; l = l->next)
			g_hash_table_insert (state->images, l->data,
					     g_strdup_printf ("Image%i", image_n++));
		g_slist_free (images);

		gsf_xml_out_start_element (state->xml, TABLE "table");
		gsf_xml_out_add_cstr (state->xml, TABLE "name", sheet->name_unquoted);

		style_name = table_style_name (state, sheet);
		gsf_xml_out_add_cstr (state->xml, TABLE "style-name", style_name);
		g_free (style_name);

		odf_add_bool (state->xml, TABLE "print", !sheet->print_info->do_not_print);

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

		/* writing shapes with absolute anchors */
		objects = odf_sheet_objects_get (sheet, NULL);
		if (objects != NULL) {
			gsf_xml_out_start_element (state->xml, TABLE "shapes");
			odf_write_objects (state, objects);
			gsf_xml_out_end_element (state->xml);
			g_slist_free (objects);
		}

		odf_write_sheet_controls (state);
		odf_write_sheet (state);
		if (state->odf_version > 101 && sheet->names) {
			gsf_xml_out_start_element (state->xml, TABLE "named-expressions");
			gnm_sheet_foreach_name (sheet,
						(GHFunc)&odf_write_named_expression, state);
			gsf_xml_out_end_element (state->xml); /* </table:named-expressions> */
		}
		if (state->with_extension) {
			GSList *ptr, *copy;
			SheetView const *sv = sheet_get_view (sheet, state->wbv);
			if (sv) {
				gsf_xml_out_start_element (state->xml, GNMSTYLE "selections");
				gsf_xml_out_add_int (state->xml, GNMSTYLE "cursor-col", sv->edit_pos_real.col);
				gsf_xml_out_add_int (state->xml, GNMSTYLE "cursor-row", sv->edit_pos_real.row);

				/* Insert the selections in REVERSE order */
				copy = g_slist_copy (sv->selections);
				ptr = copy = g_slist_reverse (copy);
				for (; ptr != NULL ; ptr = ptr->next) {
					GnmRange const *r = ptr->data;
					gsf_xml_out_start_element (state->xml, GNMSTYLE "selection");
					odf_add_range (state, r);
					gsf_xml_out_end_element (state->xml); /* </gnm:selection> */
				}
				g_slist_free (copy);

				gsf_xml_out_end_element (state->xml); /* </gnm:selections> */
			}
		}
		gnm_xml_out_end_element_check (state->xml, TABLE "table");

		has_autofilters |= (sheet->filters != NULL);
		odf_update_progress (state, state->sheet_progress);
	}

	gsf_xml_out_start_element (state->xml, TABLE "named-expressions");
	workbook_foreach_name
		(state->wb, (state->odf_version > 101),
		 (GHFunc)&odf_write_named_expression, state);
	gsf_xml_out_end_element (state->xml); /* </table:named-expressions> */

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

	gnm_xml_out_end_element_check (state->xml, OFFICE "document-content");
	g_object_unref (state->xml);
	state->xml = NULL;
}

/*****************************************************************************/

static void
odf_write_xl_style (char const *xl, char const *name, GnmOOExport *state)
{
	GOFormat *format;
	if (xl == NULL)
		xl = "General";
	format = go_format_new_from_XL (xl);
	go_format_output_to_odf (state->xml, format, 0, name,
				 state->with_extension);
	go_format_unref (format);
}

static void
odf_render_tab (GnmOOExport *state, G_GNUC_UNUSED char const *args)
{
	gsf_xml_out_simple_element (state->xml, TEXT "sheet-name", NULL);
}

static void
odf_render_page (GnmOOExport *state, G_GNUC_UNUSED char const *args)
{
	gsf_xml_out_start_element (state->xml, TEXT "page-number");
	gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "num-format", "1");
	/* odf_add_bool (state->xml, STYLE "num-letter-sync", TRUE); */
	gsf_xml_out_end_element (state->xml);
}

static void
odf_render_pages (GnmOOExport *state, G_GNUC_UNUSED char const *args)
{
	gsf_xml_out_start_element (state->xml, TEXT "page-count");
	gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "num-format", "1");
	/* odf_add_bool (state->xml, STYLE "num-letter-sync", TRUE); */
	gsf_xml_out_end_element (state->xml);
}

static void
odf_render_date (GnmOOExport *state, char const *args)
{
	const char *style_name = NULL;

	if (args != NULL)
		style_name = xl_find_format_xl (state, args);

	gsf_xml_out_start_element (state->xml, TEXT "date");
	if (style_name)
		gsf_xml_out_add_cstr_unchecked
			(state->xml, STYLE "data-style-name", style_name);
	gsf_xml_out_end_element (state->xml);
}

static void
odf_render_date_to_xl (GnmOOExport *state, char const *args)
{
	if (args != NULL)
		xl_find_format_xl (state, args);
}

static void
odf_render_time (GnmOOExport *state, char const *args)
{
	const char *style_name = NULL;

	if (args != NULL)
		style_name = xl_find_format_xl (state, args);

	gsf_xml_out_start_element (state->xml, TEXT "time");
	if (style_name)
		gsf_xml_out_add_cstr_unchecked
			(state->xml, STYLE "data-style-name", style_name);
	gsf_xml_out_end_element (state->xml);
}
static void
odf_render_time_to_xl (GnmOOExport *state, char const *args)
{
	if (args != NULL)
		xl_find_format_xl (state, args);
}

static void
odf_render_file (GnmOOExport *state, G_GNUC_UNUSED char const *args)
{
	gsf_xml_out_start_element (state->xml, TEXT "file-name");
	gsf_xml_out_add_cstr_unchecked (state->xml, TEXT "display", "name-and-extension");
	gsf_xml_out_end_element (state->xml);
}

static void
odf_render_path (GnmOOExport *state, G_GNUC_UNUSED char const *args)
{
	gsf_xml_out_start_element (state->xml, TEXT "file-name");
	gsf_xml_out_add_cstr_unchecked (state->xml, TEXT "display", "path");
	gsf_xml_out_end_element (state->xml);
}

static void
odf_render_cell (GnmOOExport *state, char const *args)
{
	GnmExprTop const *texpr = NULL;
	GnmParsePos pp;
	char *formula, *full_formula;
	GnmConventions *convs;

	if (args) {
		convs = gnm_xml_io_conventions ();
		parse_pos_init_sheet (&pp, state->sheet);
		if (args && (g_str_has_prefix (args, "rep|")))
			args += 4;
		texpr = gnm_expr_parse_str (args, &pp, GNM_EXPR_PARSE_DEFAULT,
				    convs, NULL);
		gnm_conventions_unref (convs);
		if (texpr) {
			formula = gnm_expr_top_as_string (texpr, &pp, state->conv);
			gnm_expr_top_unref (texpr);
			full_formula = g_strdup_printf ("of:=%s", formula);
			g_free (formula);
		}
	}
	gsf_xml_out_start_element (state->xml, TEXT "expression");
	gsf_xml_out_add_cstr_unchecked (state->xml, TEXT "display", "value");
	if (texpr) {
		gsf_xml_out_add_cstr (state->xml, TEXT "formula",
				      full_formula);
		g_free (full_formula);
	}

	gsf_xml_out_end_element (state->xml);
}

typedef struct {
	char const *name;
	void (*render)(GnmOOExport *state, char const *args);
	char *name_trans;
} render_ops_t;

static render_ops_t odf_render_ops [] = {
	{ N_("tab"),   odf_render_tab,   NULL},
	{ N_("page"),  odf_render_page,  NULL},
	{ N_("pages"), odf_render_pages, NULL},
	{ N_("date"),  odf_render_date,  NULL},
	{ N_("time"),  odf_render_time,  NULL},
	{ N_("file"),  odf_render_file,  NULL},
	{ N_("path"),  odf_render_path,  NULL},
	{ N_("cell"),  odf_render_cell,  NULL},
	{ NULL, NULL, NULL },
};

static render_ops_t odf_render_ops_to_xl [] = {
	{ N_("tab"),   NULL,                  NULL},
	{ N_("page"),  NULL,                  NULL},
	{ N_("pages"), NULL,                  NULL},
	{ N_("date"),  odf_render_date_to_xl, NULL},
	{ N_("time"),  odf_render_time_to_xl, NULL},
	{ N_("file"),  NULL,                  NULL},
	{ N_("path"),  NULL,                  NULL},
	{ N_("cell"),  NULL,                  NULL},
	{ NULL, NULL, NULL },
};

static void
ods_render_ops_clear (render_ops_t *render_ops)
{
	int i;

	for (i = 0; render_ops [i].name; i++) {
		g_free (render_ops[i].name_trans);
		render_ops[i].name_trans =  NULL;
	}
}

/*
 * Renders an opcode.  The opcodes can take an argument by adding trailing ':'
 * to the opcode and then a number format code
 */
static void
odf_render_opcode (GnmOOExport *state, char /* non-const */ *opcode,
		   render_ops_t *render_ops)
{
	char *args;
	char *opcode_trans;
	int i;

	args = g_utf8_strchr (opcode, -1, ':');
	if (args) {
		*args = 0;
		args++;
	}
	opcode_trans = g_utf8_casefold (opcode, -1);

	for (i = 0; render_ops [i].name; i++) {
		if (render_ops [i].name_trans == NULL) {
			render_ops [i].name_trans
				= g_utf8_casefold (_(render_ops [i].name), -1);
		}

		if (((g_ascii_strcasecmp (render_ops [i].name, opcode) == 0) ||
		    (g_utf8_collate (render_ops [i].name_trans, opcode_trans) == 0))
		    && (render_ops [i].render != NULL)){
			(*render_ops [i].render)(state, args);
		}
	}
	g_free (opcode_trans);
}

static void
odf_hf_region_to_xl_styles (GnmOOExport *state, char const *format)
{
	char const *p;

	if (format == NULL)
		return;

	for (p = format; *p; p = g_utf8_next_char(p)) {
		if (*p == '&' && p[1] == '[') {
			char const *start;

			p += 2;
			start = p;
			while (*p && (*p != ']'))
				p++;

			if (*p == ']') {
				char *operation = g_strndup (start, p - start);
				odf_render_opcode (state, operation, odf_render_ops_to_xl);
				g_free (operation);
			} else
				break;
		}
	}
}

/*
 *  When we write the master styles we need certain data style. Here we are making
 *  sure that those data styles were in fact written.
 */
static void
odf_master_styles_to_xl_styles (GnmOOExport *state)
{
	int i;

	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet const *sheet = workbook_sheet_by_index (state->wb, i);

		if (sheet->print_info->page_setup == NULL)
			gnm_print_info_load_defaults (sheet->print_info);

		if (sheet->print_info->header != NULL) {
			odf_hf_region_to_xl_styles
				(state, sheet->print_info->header->left_format);
			odf_hf_region_to_xl_styles
				(state, sheet->print_info->header->middle_format);
			odf_hf_region_to_xl_styles
				(state, sheet->print_info->header->right_format);
		}
		if (sheet->print_info->footer != NULL) {
			odf_hf_region_to_xl_styles
				(state, sheet->print_info->footer->left_format);
			odf_hf_region_to_xl_styles
				(state, sheet->print_info->footer->middle_format);
			odf_hf_region_to_xl_styles
				(state, sheet->print_info->footer->right_format);
		}
	}
}

static void
odf_write_hf_region (GnmOOExport *state, char const *format, char const *id)
{
	gboolean pp = TRUE;
	char const *p;
	GString *text;

	if (format == NULL)
		return;

	gsf_xml_out_start_element (state->xml, id);
	g_object_get (G_OBJECT (state->xml), "pretty-print", &pp, NULL);
	g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
	gsf_xml_out_start_element (state->xml, TEXT "p");

	text = g_string_new (NULL);
	for (p = format; *p; p = g_utf8_next_char(p)) {
		if (*p == '&' && p[1] == '[') {
			char const *start;

			p += 2;
			start = p;
			while (*p && (*p != ']'))
				p++;

			if (*p == ']') {
				char *operation = g_strndup (start, p - start);
				if (text->len > 0) {
					gsf_xml_out_simple_element
						(state->xml, TEXT "span", text->str);
					g_string_truncate (text, 0);
				}
				odf_render_opcode (state, operation, odf_render_ops);
				g_free (operation);
			} else
				break;
		} else
			g_string_append_len (text, p, g_utf8_next_char(p) - p);
	}
	if (text->len > 0)
		gsf_xml_out_simple_element (state->xml, TEXT "span", text->str);
	g_string_free (text, TRUE);

	gsf_xml_out_end_element (state->xml); /* </text:p> */
	g_object_set (G_OBJECT (state->xml), "pretty-print", pp, NULL);
	gsf_xml_out_end_element (state->xml); /* id */
}

static void
odf_write_hf (GnmOOExport *state, GnmPrintInformation *pi, char const *id, gboolean header)
{
	GnmPrintHF *hf = header ? pi->header : pi->footer;
	double page_margin;
	double hf_height;
	GtkPageSetup *gps = gnm_print_info_get_page_setup (pi);

	if (hf == NULL)
		return;

	if (header) {
		page_margin = gtk_page_setup_get_top_margin (gps, GTK_UNIT_POINTS);
		hf_height = pi->edge_to_below_header - page_margin;
	} else {
		page_margin = gtk_page_setup_get_bottom_margin (gps, GTK_UNIT_POINTS);
		hf_height = pi->edge_to_above_footer - page_margin;
	}

	gsf_xml_out_start_element (state->xml, id);
	odf_add_bool (state->xml, STYLE "display", hf_height > 0);

	odf_write_hf_region (state, hf->left_format, STYLE "region-left");
	odf_write_hf_region (state, hf->middle_format, STYLE "region-center");
	odf_write_hf_region (state, hf->right_format, STYLE "region-right");
	gsf_xml_out_end_element (state->xml); /* id */
}

static void
odf_store_data_style_for_style_with_name (GnmStyleRegion *sr, G_GNUC_UNUSED char const *name, GnmOOExport *state)
{
	GnmStyle const *style = sr->style;

	if (gnm_style_is_element_set (style, MSTYLE_FORMAT)) {
		GOFormat const *format = gnm_style_get_format(style);
		if (format != NULL && !go_format_is_markup (format) && !go_format_is_general (format)) {
			xl_find_format (state, format);
		}
	}
}

static int
by_key_str (gpointer key_a, G_GNUC_UNUSED gpointer val_a,
	    gpointer key_b, G_GNUC_UNUSED gpointer val_b,
	    G_GNUC_UNUSED gpointer user)
{
	return strcmp (key_a, key_b);
}

static int
by_value_str (G_GNUC_UNUSED gpointer key_a, gpointer val_a,
	      G_GNUC_UNUSED gpointer key_b, gpointer val_b,
	      G_GNUC_UNUSED gpointer user)
{
	return strcmp (val_a, val_b);
}

static void
odf_write_office_styles (GnmOOExport *state)
{
	gsf_xml_out_start_element (state->xml, OFFICE "styles");

	/* We need to make sure all the data styles for the named styles are included */
	g_hash_table_foreach (state->named_cell_style_regions, (GHFunc) odf_store_data_style_for_style_with_name, state);

	gnm_hash_table_foreach_ordered
		(state->xl_styles,
		 (GHFunc) odf_write_xl_style,
		 by_value_str,
		 state);

	gnm_hash_table_foreach_ordered
		(state->named_cell_style_regions,
		 (GHFunc) odf_save_this_style_with_name,
		 by_value_str,
		 state);

	gnm_hash_table_foreach_ordered
		(state->font_sizes,
		 (GHFunc) odf_write_font_sizes,
		 by_key_str,
		 state);

	gnm_hash_table_foreach_ordered
		(state->text_colours,
		 (GHFunc) odf_write_text_colours,
		 by_key_str,
		 state);

	if (state->default_style_region->style != NULL) {
		gsf_xml_out_start_element (state->xml, STYLE "default-style");
		gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "family", "table-cell");
		odf_write_style (state, state->default_style_region->style,
				 &state->default_style_region->range, TRUE);
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

	gnm_hash_table_foreach_ordered
		(state->graph_dashes,
		 (GHFunc) odf_write_dash_info,
		 by_key_str,
		 state);

	gnm_hash_table_foreach_ordered
		(state->graph_hatches,
		 (GHFunc) odf_write_hatch_info,
		 by_value_str,
		 state);

	gnm_hash_table_foreach_ordered
		(state->graph_gradients,
		 (GHFunc) odf_write_gradient_info,
		 by_value_str,
		 state);

	gnm_hash_table_foreach_ordered
		(state->graph_fill_images,
		 (GHFunc) odf_write_fill_images_info,
		 by_value_str,
		 state);

	gnm_hash_table_foreach_ordered
		(state->arrow_markers,
		 (GHFunc) odf_write_arrow_marker_info,
		 by_value_str,
		 state);

	g_hash_table_remove_all (state->graph_dashes);
	g_hash_table_remove_all (state->graph_hatches);
	g_hash_table_remove_all (state->graph_gradients);
	g_hash_table_remove_all (state->graph_fill_images);
	g_hash_table_remove_all (state->arrow_markers);

	gsf_xml_out_end_element (state->xml); /* </office:styles> */
}

static void
odf_write_hf_style (GnmOOExport *state, GnmPrintInformation *pi, char const *id, gboolean header)
{
	GnmPrintHF *hf = header ? pi->header : pi->footer;
	double page_margin;
	double hf_height;
	GtkPageSetup *gps = gnm_print_info_get_page_setup (pi);

	if (hf == NULL)
		return;

	if (header) {
		page_margin = gtk_page_setup_get_top_margin (gps, GTK_UNIT_POINTS);
		hf_height = pi->edge_to_below_header - page_margin;
	} else {
		page_margin = gtk_page_setup_get_bottom_margin (gps, GTK_UNIT_POINTS);
		hf_height = pi->edge_to_above_footer - page_margin;
	}

	gsf_xml_out_start_element (state->xml, id);
	gsf_xml_out_start_element (state->xml, STYLE "header-footer-properties");

	gsf_xml_out_add_cstr_unchecked (state->xml, FOSTYLE "border", "none");
	gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "shadow", "none");
	odf_add_pt (state->xml, FOSTYLE "padding", 0.0);
	odf_add_pt (state->xml, FOSTYLE "margin", 0.0);
	odf_add_pt (state->xml, FOSTYLE "min-height", hf_height);
	odf_add_pt (state->xml, SVG "height", hf_height);
	odf_add_bool (state->xml, STYLE "dynamic-spacing", TRUE);

	gsf_xml_out_end_element (state->xml); /* header-footer-properties */
	gsf_xml_out_end_element (state->xml); /* id */
}


static void
odf_write_page_layout (GnmOOExport *state, GnmPrintInformation *pi,
		       Sheet const *sheet)
{
	static char const *centre_type [] = {
		"none"        ,
		"horizontal"  ,
		"vertical"    ,
		"both"        ,
		NULL          };

	char *name =  page_layout_name (state, pi);
	GtkPageSetup *gps = gnm_print_info_get_page_setup (pi);
	int i;
	GtkPageOrientation orient = gtk_page_setup_get_orientation (gps);
	gboolean landscape = !(orient == GTK_PAGE_ORIENTATION_PORTRAIT ||
			       orient == GTK_PAGE_ORIENTATION_REVERSE_PORTRAIT);
	GString *gstr = g_string_new ("charts drawings objects");

	gsf_xml_out_start_element (state->xml, STYLE "page-layout");
	gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "name", name);
	g_free (name);
	gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "page-usage", "all");

	gsf_xml_out_start_element (state->xml, STYLE "page-layout-properties");
	odf_add_pt (state->xml, FOSTYLE "margin-top",
		    gtk_page_setup_get_top_margin (gps, GTK_UNIT_POINTS));
	odf_add_pt (state->xml, FOSTYLE "margin-bottom",
		    gtk_page_setup_get_bottom_margin (gps, GTK_UNIT_POINTS));
	odf_add_pt (state->xml, FOSTYLE "margin-left",
		    gtk_page_setup_get_left_margin (gps, GTK_UNIT_POINTS));
	odf_add_pt (state->xml, FOSTYLE "margin-right",
		    gtk_page_setup_get_right_margin (gps, GTK_UNIT_POINTS));
	odf_add_pt (state->xml, FOSTYLE "page-width",
		    gtk_page_setup_get_paper_width (gps, GTK_UNIT_POINTS));
	odf_add_pt (state->xml, FOSTYLE "page-height",
		    gtk_page_setup_get_paper_height (gps, GTK_UNIT_POINTS));
	i = (pi->center_horizontally ? 1 : 0) | (pi->center_vertically ? 2 : 0);
	gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "table-centering",
					centre_type [i]);
	gsf_xml_out_add_cstr_unchecked
		(state->xml, STYLE "print-page-order",
		 pi->print_across_then_down ? "ltr" : "ttb");
	gsf_xml_out_add_cstr_unchecked
		(state->xml, STYLE "writing-mode",
		 sheet->text_is_rtl ? "rl-tb" : "lr-tb");
	gsf_xml_out_add_cstr_unchecked
		(state->xml, STYLE "print-orientation",
		 landscape ? "landscape" : "portrait");

	if (pi->print_grid_lines)
		g_string_append (gstr, " grid");
	if (pi->print_titles)
		g_string_append (gstr, " headers");
	if (pi->comment_placement != GNM_PRINT_COMMENTS_NONE)
		g_string_append (gstr, " annotations");
	gsf_xml_out_add_cstr_unchecked
		(state->xml, STYLE "print", gstr->str);

	switch (pi->scaling.type) {
	case PRINT_SCALE_FIT_PAGES: {
		int x = pi->scaling.dim.cols;
		int y = pi->scaling.dim.rows;
		if (state->with_extension) {
			/* LO uses style:scale-to-X and style:scale-to-Y but */
			/* these are not valid in the style: namespace       */
			/* So to be understood by LO we would need to write  */
			/* invalid ODF. They should be using one of their    */
			/* extension namespace, but are not!                 */
			if (x > 0)
				gsf_xml_out_add_int (state->xml, GNMSTYLE "scale-to-X", x);
			if (y > 0)
				gsf_xml_out_add_int (state->xml, GNMSTYLE "scale-to-Y", y);
		} else {
			/* ODF 1.2 only allows us to specify the total number of pages. */
			int x = pi->scaling.dim.cols;
			int y = pi->scaling.dim.rows;
			if (x > 0 && y > 0)
				gsf_xml_out_add_int (state->xml, STYLE "scale-to-pages", x*y);
		}
		break;
	}
	case PRINT_SCALE_PERCENTAGE:
		odf_add_percent (state->xml, STYLE "scale-to", pi->scaling.percentage.x/100);
		break;
	default:
		odf_add_percent (state->xml, STYLE "scale-to", 1.);
	}

	if (state->with_extension) {
		g_string_truncate (gstr, 0);
		if (pi->comment_placement == GNM_PRINT_COMMENTS_AT_END)
			g_string_append (gstr, " annotations_at_end");
		if (pi->print_black_and_white)
			g_string_append (gstr, " black_n_white");
		if (pi->print_as_draft)
			g_string_append (gstr, " draft");
		if (pi->print_even_if_only_styles)
			g_string_append (gstr, " print_even_if_only_styles");
		switch (pi->error_display) {
		case GNM_PRINT_ERRORS_AS_BLANK:
			g_string_append (gstr, " errors_as_blank");
			break;
		case GNM_PRINT_ERRORS_AS_DASHES:
			g_string_append (gstr, " errors_as_dashes");
			break;
		case GNM_PRINT_ERRORS_AS_NA:
			g_string_append (gstr, " errors_as_na");
			break;
		default:
		case GNM_PRINT_ERRORS_AS_DISPLAYED:
			break;
		}
		gsf_xml_out_add_cstr_unchecked
			(state->xml, GNMSTYLE "style-print", gstr->str);
	}

	g_string_free (gstr, TRUE);

	gsf_xml_out_end_element (state->xml); /* </style:page-layout-properties> */

	odf_write_hf_style (state, pi, STYLE "header-style", TRUE);
	odf_write_hf_style (state, pi, STYLE "footer-style", FALSE);


	gsf_xml_out_end_element (state->xml); /* </style:page-layout> */
}

static void
odf_write_automatic_styles (GnmOOExport *state)
{
	int i;

	gsf_xml_out_start_element (state->xml, OFFICE "automatic-styles");

	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet const *sheet = workbook_sheet_by_index (state->wb, i);
		odf_write_page_layout (state, sheet->print_info, sheet);
	}

	gsf_xml_out_end_element (state->xml); /* </office:automatic-styles> */
}

static void
odf_write_master_styles (GnmOOExport *state)
{
	int i;

	gsf_xml_out_start_element (state->xml, OFFICE "master-styles");

	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet const *sheet = workbook_sheet_by_index (state->wb, i);
		char *mp_name  = table_master_page_style_name (state, sheet);
		char *name =  page_layout_name (state, sheet->print_info);

		gsf_xml_out_start_element (state->xml, STYLE "master-page");
		gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "name", mp_name);
		gsf_xml_out_add_cstr (state->xml, STYLE "display-name", sheet->name_unquoted);
		gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "page-layout-name",
						name);

		odf_write_hf (state, sheet->print_info, STYLE "header", TRUE);
		odf_write_hf (state, sheet->print_info, STYLE "footer", FALSE);

		gsf_xml_out_end_element (state->xml); /* </master-page> */
		g_free (mp_name);
		g_free (name);
	}

	gsf_xml_out_end_element (state->xml); /* </master-styles> */
}

static void
odf_write_styles (GnmOOExport *state, GsfOutput *child)
{
	int i;

	state->xml = create_new_xml_child (state, child);
	gsf_xml_out_start_element (state->xml, OFFICE "document-styles");
	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version",
					state->odf_version_string);

	odf_master_styles_to_xl_styles (state);

	odf_write_office_styles (state);
	odf_write_automatic_styles (state);
	odf_write_master_styles (state);

	gnm_xml_out_end_element_check (state->xml, OFFICE "document-styles");

	g_object_unref (state->xml);
	state->xml = NULL;
}

/*****************************************************************************/

static void
odf_write_meta (GnmOOExport *state, GsfOutput *child)
{
	GsfXMLOut *xml = create_new_xml_child (state, child);
	GsfDocMetaData *meta = go_doc_get_meta_data (GO_DOC (state->wb));
	GValue *val = g_new0 (GValue, 1);
	GsfDocProp *prop = gsf_doc_meta_data_steal (meta, GSF_META_NAME_GENERATOR);

	g_value_init (val, G_TYPE_STRING);
	g_value_set_string (val, PACKAGE_NAME "/" VERSION);

	gsf_doc_meta_data_insert  (meta, g_strdup (GSF_META_NAME_GENERATOR), val);
	gsf_doc_meta_data_write_to_odf (meta, xml);
	gsf_doc_meta_data_remove (meta,GSF_META_NAME_GENERATOR);
	if (prop != NULL)
		gsf_doc_meta_data_store (meta, prop);
	g_object_unref (xml);
}

static void
odf_write_meta_graph (G_GNUC_UNUSED GnmOOExport *state, GsfOutput *child)
{
	GsfXMLOut *xml = create_new_xml_child (state, child);
	GsfDocMetaData *meta = gsf_doc_meta_data_new ();
	GValue *val = g_new0 (GValue, 1);

	g_value_init (val, G_TYPE_STRING);
	g_value_set_string (val, PACKAGE_NAME "/" VERSION);

	gsf_doc_meta_data_insert  (meta, g_strdup (GSF_META_NAME_GENERATOR), val);
	gsf_doc_meta_data_write_to_odf (meta, xml);

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
	char *d_string = NULL;
	char *box_string = NULL;
	int a = (int) (arrow->a + 0.5);
	int b = (int) (arrow->b + 0.5);
	int c = (int) (arrow->c + 0.5);

	gsf_xml_out_start_element (state->xml, DRAW "marker");
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "name", name);

	if (state->with_extension) {
		gsf_xml_out_add_int (state->xml, GNMSTYLE "arrow-type", arrow->typ);
		go_xml_out_add_double (state->xml, GNMSTYLE "arrow-a", arrow->a);
		go_xml_out_add_double (state->xml, GNMSTYLE "arrow-b", arrow->b);
		go_xml_out_add_double (state->xml, GNMSTYLE "arrow-c", arrow->c);
	}

	switch (arrow->typ) {
	case GO_ARROW_NONE:
		box_string = g_strdup ("-1 -1 1 1");
		d_string = g_strdup ("M 0,0");
		break;
	case GO_ARROW_KITE:
		box_string = g_strdup_printf
			("%i 0 %i %i", -c, c, a < b ? b : a);
		d_string = g_strdup_printf
			("M 0,0 %i,%i 0,%i %i,%i z", -c, b, a, c, b);
		break;
	case GO_ARROW_OVAL:
		box_string = g_strdup_printf ("%d %d %d %d", -a, -a, a, a);
		d_string = g_strdup_printf
			("M 0,0 m %d,0 a %d,%d 0 1,0 %d,0 a %d,%d 0 1,0 %d,0",
			 -a, a, b, 2*a, a, b, -2 * a);
		break;
	default:
		box_string = g_strdup ("-100 -100 100 100");
		d_string = g_strdup ("M 0,-100 -100,-50 0,100 100,-50 z");
		break;
	}

	if (box_string)
		gsf_xml_out_add_cstr (state->xml, SVG "viewBox", box_string);
	if (d_string)
		gsf_xml_out_add_cstr (state->xml, SVG "d", d_string);
	g_free (box_string);
	g_free (d_string);

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
		{GO_GRADIENT_W_TO_E, "linear", 270},
		{GO_GRADIENT_E_TO_W, "linear", 90},
		{GO_GRADIENT_W_TO_E_MIRRORED, "axial", 270},
		{GO_GRADIENT_E_TO_W_MIRRORED, "axial", 90},
		{GO_GRADIENT_NW_TO_SE, "linear", 225},
		{GO_GRADIENT_SE_TO_NW, "linear", 45},
		{GO_GRADIENT_NW_TO_SE_MIRRORED, "axial", 225},
		{GO_GRADIENT_SE_TO_NW_MIRRORED, "axial", 45},
		{GO_GRADIENT_NE_TO_SW, "linear", 135},
		{GO_GRADIENT_SW_TO_NE, "linear", 315},
		{GO_GRADIENT_SW_TO_NE_MIRRORED, "axial", 315},
		{GO_GRADIENT_NE_TO_SW_MIRRORED, "axial", 135},
	};
	int i;

	gsf_xml_out_start_element (state->xml, DRAW "gradient");
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "name", name);

	color = odf_go_color_to_string (style->fill.pattern.back);
	gsf_xml_out_add_cstr_unchecked (state->xml, DRAW "start-color", color);
	g_free (color);

	if (style->fill.gradient.brightness >= 0.0 && state->with_extension)
		go_xml_out_add_double (state->xml, GNMSTYLE "brightness",
				       style->fill.gradient.brightness);

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
	gboolean new = (state->odf_version > 101);

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
		if (dot_1 == 0)
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
			if (dot_1 == 0)
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

	state->xml = create_new_xml_child (state, child);
	gsf_xml_out_start_element (state->xml, OFFICE "document-styles");
	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version",
					state->odf_version_string);
	gsf_xml_out_start_element (state->xml, OFFICE "styles");

	gnm_hash_table_foreach_ordered
		(state->graph_dashes,
		 (GHFunc) odf_write_dash_info,
		 by_key_str,
		 state);

	gnm_hash_table_foreach_ordered
		(state->graph_hatches,
		 (GHFunc) odf_write_hatch_info,
		 by_value_str,
		 state);

	gnm_hash_table_foreach_ordered
		(state->graph_gradients,
		 (GHFunc) odf_write_gradient_info,
		 by_value_str,
		 state);

	gnm_hash_table_foreach_ordered
		(state->graph_fill_images,
		 (GHFunc) odf_write_fill_images_info,
		 by_value_str,
		 state);

	gnm_hash_table_foreach_ordered
		(state->xl_styles,
		 (GHFunc) odf_write_xl_style,
		 by_value_str,
		 state);

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

	gsf_xml_out_start_element (state->xml, CONFIG "config-item");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", GNMSTYLE "active-sheet");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "string");
	gsf_xml_out_add_cstr (state->xml, NULL,
			      (wb_view_cur_sheet (state->wbv))->name_unquoted);
	gsf_xml_out_end_element (state->xml); /* </config:config-item> */

	gsf_xml_out_start_element (state->xml, CONFIG "config-item");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", GNMSTYLE "geometry-width");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
	gsf_xml_out_add_int (state->xml, NULL,
			     state->wbv->preferred_width);
	gsf_xml_out_end_element (state->xml); /* </config:config-item> */

	gsf_xml_out_start_element (state->xml, CONFIG "config-item");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", GNMSTYLE "geometry-height");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
	gsf_xml_out_add_int (state->xml, NULL,
			     state->wbv->preferred_height);
	gsf_xml_out_end_element (state->xml); /* </config:config-item> */

	gsf_xml_out_end_element (state->xml); /* </config:config-item-set> */
}

static void
odf_write_ooo_settings (GnmOOExport *state)
{
	GPtrArray *sheets;
	unsigned ui;

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

	sheets = workbook_sheets (state->wb);
	for (ui = 0; ui < sheets->len; ui++) {
		Sheet *sheet = g_ptr_array_index (sheets, ui);
		SheetView *sv = sheet_get_view (sheet, state->wbv);
		gsf_xml_out_start_element (state->xml, CONFIG "config-item-map-entry");
		gsf_xml_out_add_cstr (state->xml, CONFIG "name", sheet->name_unquoted);
		if (state->odf_version < 103  && sheet->tab_color != NULL
		    && !sheet->tab_color->is_auto) {
			/* Not used by LO 3.3.3 and later */
			gsf_xml_out_start_element (state->xml, CONFIG "config-item");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "TabColor");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
			gsf_xml_out_add_int (state->xml, NULL, sheet->tab_color->go_color >> 8);
			gsf_xml_out_end_element (state->xml); /* </config:config-item> */
		}
		gsf_xml_out_start_element (state->xml, CONFIG "config-item");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "CursorPositionX");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
		gsf_xml_out_add_int (state->xml, NULL, sv->edit_pos.col);
		gsf_xml_out_end_element (state->xml); /* </config:config-item> */
		gsf_xml_out_start_element (state->xml, CONFIG "config-item");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "CursorPositionY");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
		gsf_xml_out_add_int (state->xml, NULL, sv->edit_pos.row);
		gsf_xml_out_end_element (state->xml); /* </config:config-item> */

		gsf_xml_out_start_element (state->xml, CONFIG "config-item");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "ZoomValue");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
		gsf_xml_out_add_int (state->xml, NULL, (int) gnm_round (sheet->last_zoom_factor_used * 100));
		gsf_xml_out_end_element (state->xml); /* </config:config-item> */

		gsf_xml_out_start_element (state->xml, CONFIG "config-item");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "ShowGrid");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "boolean");
		odf_add_bool (state->xml, NULL, !sheet->hide_grid);
		gsf_xml_out_end_element (state->xml); /* </config:config-item> */

		gsf_xml_out_start_element (state->xml, CONFIG "config-item");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "HasColumnRowHeaders");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "boolean");
		odf_add_bool (state->xml, NULL,
			      (!sheet->hide_col_header) || !sheet->hide_row_header);
		gsf_xml_out_end_element (state->xml); /* </config:config-item> */

		gsf_xml_out_start_element (state->xml, CONFIG "config-item");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "ShowZeroValues");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "boolean");
		odf_add_bool (state->xml, NULL, !sheet->hide_zero);
		gsf_xml_out_end_element (state->xml); /* </config:config-item> */

		if (gnm_sheet_view_is_frozen (sv)) {
			gsf_xml_out_start_element (state->xml, CONFIG "config-item");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "HorizontalSplitMode");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "short");
			gsf_xml_out_add_int (state->xml, NULL, 2);
			gsf_xml_out_end_element (state->xml); /* </config:config-item> */
			gsf_xml_out_start_element (state->xml, CONFIG "config-item");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "VerticalSplitMode");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "short");
			gsf_xml_out_add_int (state->xml, NULL, 2);
			gsf_xml_out_end_element (state->xml); /* </config:config-item> */
			gsf_xml_out_start_element (state->xml, CONFIG "config-item");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "HorizontalSplitPosition");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
			gsf_xml_out_add_int (state->xml, NULL, sv->unfrozen_top_left.col);
			gsf_xml_out_end_element (state->xml); /* </config:config-item> */
			gsf_xml_out_start_element (state->xml, CONFIG "config-item");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "VerticalSplitPosition");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
			gsf_xml_out_add_int (state->xml, NULL, sv->unfrozen_top_left.row);
			gsf_xml_out_end_element (state->xml); /* </config:config-item> */
			gsf_xml_out_start_element (state->xml, CONFIG "config-item");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "PositionLeft");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
			gsf_xml_out_add_int (state->xml, NULL, 0);
			gsf_xml_out_end_element (state->xml); /* </config:config-item> */
			gsf_xml_out_start_element (state->xml, CONFIG "config-item");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "PositionRight");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
			gsf_xml_out_add_int (state->xml, NULL, sv->initial_top_left.col);
			gsf_xml_out_end_element (state->xml); /* </config:config-item> */
		} else {
			gsf_xml_out_start_element (state->xml, CONFIG "config-item");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "PositionLeft");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
			gsf_xml_out_add_int (state->xml, NULL, sv->initial_top_left.col);
			gsf_xml_out_end_element (state->xml); /* </config:config-item> */
			gsf_xml_out_start_element (state->xml, CONFIG "config-item");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "PositionRight");
			gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
			gsf_xml_out_add_int (state->xml, NULL, 0);
			gsf_xml_out_end_element (state->xml); /* </config:config-item> */
		}
		gsf_xml_out_start_element (state->xml, CONFIG "config-item");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "PositionTop");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
		gsf_xml_out_add_int (state->xml, NULL, 0);
		gsf_xml_out_end_element (state->xml); /* </config:config-item> */
		gsf_xml_out_start_element (state->xml, CONFIG "config-item");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "PositionBottom");
		gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "int");
		gsf_xml_out_add_int (state->xml, NULL, sv->initial_top_left.row);
		gsf_xml_out_end_element (state->xml); /* </config:config-item> */

		gsf_xml_out_end_element (state->xml); /* </config:config-item-map-entry> */
	}
	g_ptr_array_unref (sheets);

	gsf_xml_out_end_element (state->xml); /* </config:config-item-map-named> */

	gsf_xml_out_start_element (state->xml, CONFIG "config-item");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "name", "ActiveTable");
	gsf_xml_out_add_cstr_unchecked (state->xml, CONFIG "type", "string");
	gsf_xml_out_add_cstr (state->xml, NULL,
			      (wb_view_cur_sheet (state->wbv))->name_unquoted);
	gsf_xml_out_end_element (state->xml); /* </config:config-item> */

	gsf_xml_out_end_element (state->xml); /* </config:config-item-map-entry> */
	gsf_xml_out_end_element (state->xml); /* </config:config-item-map-indexed> */
	gsf_xml_out_end_element (state->xml); /* </config:config-item-set> */
}

static void
odf_write_settings (GnmOOExport *state, GsfOutput *child)
{
	int i;

	state->xml = create_new_xml_child (state, child);
	gsf_xml_out_start_element (state->xml, OFFICE "document-settings");
	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version",
					state->odf_version_string);

	gsf_xml_out_start_element (state->xml, OFFICE "settings");

	odf_write_gnm_settings (state);
	odf_write_ooo_settings (state);

	gsf_xml_out_end_element (state->xml); /* </office:settings> */
	gnm_xml_out_end_element_check (state->xml, OFFICE "document-settings");
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
odf_write_graph_manifest (SheetObject *graph, char const *name, GnmOOExport *state)
{
	guint i, n = odf_n_charts (state, graph);

	for (i = 0; i < n; i++) {
		char *realname = g_strdup_printf ("%s-%i", name, i);
		char *fullname = g_strdup_printf ("%s/", realname);
		odf_file_entry (state->xml, "application/vnd.oasis.opendocument.chart", fullname);
		g_free(fullname);
		fullname = g_strdup_printf ("%s/content.xml", realname);
		odf_file_entry (state->xml, "text/xml", fullname);
		g_free(fullname);
		fullname = g_strdup_printf ("%s/meta.xml", realname);
		odf_file_entry (state->xml, "text/xml", fullname);
		g_free(fullname);
		fullname = g_strdup_printf ("%s/styles.xml", realname);
		odf_file_entry (state->xml, "text/xml", fullname);
		g_free(fullname);
		fullname = g_strdup_printf ("Pictures/%s", realname);
		odf_file_entry (state->xml, "image/svg+xml", fullname);
		g_free(fullname);
		fullname = g_strdup_printf ("Pictures/%s.png", realname);
		odf_file_entry (state->xml, "image/png", fullname);
		g_free(fullname);
		g_free(realname);
	}
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
	GsfXMLOut *xml = create_new_xml_child (state, child);
	GSList *l;

	gsf_xml_out_set_doc_type (xml, "\n");
	gsf_xml_out_start_element (xml, MANIFEST "manifest");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:manifest",
		"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0");
	if (state->odf_version > 101)
		gsf_xml_out_add_cstr_unchecked (xml, MANIFEST "version",
						state->odf_version_string);
	odf_file_entry (xml, "application/vnd.oasis.opendocument.spreadsheet" ,"/");
	odf_file_entry (xml, "text/xml", "content.xml");
	odf_file_entry (xml, "text/xml", "styles.xml");
	odf_file_entry (xml, "text/xml", "meta.xml");
	odf_file_entry (xml, "text/xml", "settings.xml");

	state->xml = xml;
	gnm_hash_table_foreach_ordered
		(state->graphs,
		 (GHFunc) odf_write_graph_manifest,
		 by_value_str,
		 state);
	gnm_hash_table_foreach_ordered
		(state->images,
		 (GHFunc) odf_write_image_manifest,
		 by_value_str,
		 state);

	for (l = state->fill_image_files; l != NULL; l = l->next)
		odf_file_entry (xml, "image/png", l->data);
	g_slist_free_full (state->fill_image_files, g_free);
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
odf_write_series_lines (GnmOOExport *state, GogObject const *series)
{
	GogObjectRole const *role = gog_object_find_role_by_name (series, "Series lines");

	if (role != NULL) {
		GSList *serieslines = gog_object_get_children (series, role);
		if (serieslines != NULL && serieslines->data != NULL) {
			GogObject *obj = GOG_OBJECT (serieslines->data);
			char *style = odf_get_gog_style_name_from_obj (state, obj);

			gsf_xml_out_start_element (state->xml, GNMSTYLE "serieslines");
			gsf_xml_out_add_cstr (state->xml, CHART "style-name", style);
			gsf_xml_out_end_element (state->xml); /* </gnm:serieslines> */

			g_free (style);
		}
		g_slist_free (serieslines);
	}
}

static void
odf_write_drop_line (GnmOOExport *state, GogObject const *series, char const *drop)
{
	GogObjectRole const *role = gog_object_find_role_by_name (series, drop);

	if (role != NULL) {
		GSList *drops = gog_object_get_children
			(series, role);
		if (drops != NULL && drops->data != NULL) {
			GogObject *obj = GOG_OBJECT (drops->data);
			char *style = odf_get_gog_style_name_from_obj (state, obj);

			gsf_xml_out_start_element (state->xml, GNMSTYLE "droplines");
			gsf_xml_out_add_cstr (state->xml, CHART "style-name", style);
			gsf_xml_out_end_element (state->xml); /* </gnm:droplines> */

			g_free (style);
		}
		g_slist_free (drops);
	}
}

static void
odf_write_data_element_range (GnmOOExport *state,  GnmParsePos *pp, GnmExprTop const *texpr,
			      char const *attribute, char const *gnm_attribute)
{
	char *str;

	switch (GNM_EXPR_GET_OPER (texpr->expr)) {
	case GNM_EXPR_OP_CONSTANT:
		if (VALUE_IS_CELLRANGE (texpr->expr->constant.value)) {
			str = gnm_expr_top_as_string (texpr, pp, state->conv);
			gsf_xml_out_add_cstr (state->xml, attribute,
					      odf_strip_brackets (str));
			g_free (str);
			return;
		}
		break;
	case GNM_EXPR_OP_SET: {
		int i;
		gboolean success = TRUE;
		GnmExpr const *expr = texpr->expr;
		GString *gstr = g_string_new (NULL);
		for (i = 0; i < expr->set.argc; i++) {
			GnmExpr const *expr_arg = expr->set.argv[i];
			if ((GNM_EXPR_GET_OPER (expr_arg) == GNM_EXPR_OP_CONSTANT &&
			     VALUE_IS_CELLRANGE (expr_arg->constant.value)) ||
			    (GNM_EXPR_GET_OPER (expr_arg) == GNM_EXPR_OP_CELLREF)) {
				char *str = gnm_expr_as_string (expr_arg, pp, state->conv);
				if (gstr->len > 0)
					g_string_append_c (gstr, ' ');
				g_string_append (gstr, odf_strip_brackets (str));
				g_free (str);
			} else
				success = FALSE;
		}
		if (success) {
			gsf_xml_out_add_cstr (state->xml, attribute, gstr->str);
			g_string_free (gstr, TRUE);
			return;
		}
		g_string_free (gstr, TRUE);
		break;
	}
	case GNM_EXPR_OP_CELLREF:
		str = gnm_expr_top_as_string (texpr, pp, state->conv);
		gsf_xml_out_add_cstr (state->xml, attribute,
				      odf_strip_brackets (str));
		g_free (str);
		return;
	default:
		break;
	}

	/* ODF does not support anything but Gnumeric does */
	if (NULL != gnm_attribute) {
		str = gnm_expr_top_as_string (texpr, pp, state->conv);
		gsf_xml_out_add_cstr (state->xml, gnm_attribute, str);
		g_free (str);
	}
}

static gboolean
odf_write_data_element (GnmOOExport *state, GOData const *data, GnmParsePos *pp,
			char const *element, char const *attribute, char const *gnm_attribute)
{
	GnmExprTop const *texpr = gnm_go_data_get_expr (data);

	if (NULL != texpr) {
		char *str = gnm_expr_top_as_string (texpr, pp, state->conv);
		gsf_xml_out_start_element (state->xml, element);
		odf_write_data_element_range (state, pp, texpr, attribute, gnm_attribute);
		g_free (str);
		return TRUE;
	}
	return FALSE;
}

static void
odf_write_data_attribute (GnmOOExport *state, GOData const *data, GnmParsePos *pp,
			  char const *attribute, char const *c_attribute)
{
	GnmExprTop const *texpr = gnm_go_data_get_expr (data);

	if (NULL != texpr) {
		if (state->with_extension) {
			char *str = gnm_expr_top_as_string (texpr, pp,
							    state->conv);
			gsf_xml_out_add_cstr (state->xml, attribute,
					      odf_strip_brackets (str));
			g_free (str);
		}
		if (NULL != c_attribute) {
			GnmValue const *v = gnm_expr_top_get_constant (texpr);
			if (NULL != v && VALUE_IS_STRING (v))
				gsf_xml_out_add_cstr (state->xml, c_attribute,
						      value_peek_string (v));
			if (NULL != v && VALUE_IS_FLOAT (v))
				go_xml_out_add_double (state->xml, c_attribute,
						       value_get_as_float (v));
		}
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
odf_write_regression_curve (GnmOOExport *state, GogObjectRole const *role, GogObject const *series, GnmParsePos *pp)
{
	GSList *l, *regressions = gog_object_get_children
		(series, role);
	char *str;

	for (l = regressions; l != NULL && l->data != NULL; l = l->next) {
		GOData const *bd;
		GogObject const *regression = l->data;
		gboolean is_reg_curve = GOG_IS_REG_CURVE (regression);
		GogObject const *equation
			= is_reg_curve?
			gog_object_get_child_by_name (regression, "Equation"):
			NULL;
		str = odf_get_gog_style_name_from_obj
			(state, GOG_OBJECT (regression));
		gsf_xml_out_start_element
			(state->xml, CHART "regression-curve");
		gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
		g_free (str);

		if (is_reg_curve && state->with_extension) {
			/* Upper and lower bounds */
			bd = gog_dataset_get_dim (GOG_DATASET (regression), 0);
			if (bd != NULL)
				odf_write_data_attribute
					(state, bd, pp, GNMSTYLE "lower-bound", NULL);
			bd = gog_dataset_get_dim (GOG_DATASET (regression), 1);
			if (bd != NULL)
				odf_write_data_attribute
					(state, bd, pp, GNMSTYLE "upper-bound", NULL);
		}
		if (equation != NULL) {
			char const *eq_element, *eq_automatic, *eq_display, *eq_r;
			if (state->odf_version > 101) {
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
			odf_write_plot_style_bool (state->xml, equation,
						   "show-eq", eq_display);
			odf_write_plot_style_bool (state->xml, equation,
						   "show-r2", eq_r);
			str = odf_get_gog_style_name_from_obj
				(state, GOG_OBJECT (equation));
			gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
			g_free (str);
			odf_write_gog_position (state, equation);
			odf_write_gog_position_pts (state, equation);
			gsf_xml_out_end_element (state->xml); /* </chart:equation> */
		}

		gsf_xml_out_end_element (state->xml); /* </chart:regression-curve> */
	}
	g_slist_free (regressions);
}

static void
odf_write_attached_axis (GnmOOExport *state, char const * const axis_role, int id)
{
	GString *str = g_string_new (NULL);
	g_string_append_printf (str, "%s-%i", axis_role, id);
	gsf_xml_out_add_cstr_unchecked (state->xml, CHART "attached-axis", str->str);
	g_string_free (str, TRUE);
}

static void
odf_write_attached_axes (GnmOOExport *state, GogObject *series)
{
	GogPlot	*plot = gog_series_get_plot (GOG_SERIES (series));
	GogAxis	*axis = gog_plot_get_axis (plot, GOG_AXIS_X);
	int id;

	id = (NULL != axis) ? gog_object_get_id (GOG_OBJECT (axis)) : 0;
	if (id > 1)
		odf_write_attached_axis (state, "X-Axis", id);
	else {
		axis = gog_plot_get_axis (plot, GOG_AXIS_Z);
		id = (NULL != axis) ? gog_object_get_id (GOG_OBJECT (axis)) : 0;
		if (id > 1)
			odf_write_attached_axis (state, "Z-Axis", id);
		else {
			axis = gog_plot_get_axis (plot, GOG_AXIS_Y);
			if (NULL != axis) {
				id = gog_object_get_id (GOG_OBJECT (axis));
				odf_write_attached_axis (state, "Y-Axis", id);
			}
		}
	}
}


static void
odf_write_standard_series (GnmOOExport *state, GSList const *series, char const* class)
{
	GnmParsePos pp;
	int i;
	parse_pos_init (&pp, WORKBOOK (state->wb), NULL, 0,0 );

	for (i = 1; NULL != series ; series = series->next, i++) {
		GOData const *dat = gog_dataset_get_dim (GOG_DATASET (series->data), GOG_MS_DIM_VALUES);
		if (NULL != dat && odf_write_data_element (state, dat, &pp, CHART "series",
							   CHART "values-cell-range-address",
							   GNMSTYLE "values-cell-range-expression")) {
			GogObjectRole const *role;
			GSList *points;
			GOData const *cat = gog_dataset_get_dim (GOG_DATASET (series->data),
								 GOG_MS_DIM_LABELS);
			char *str = odf_get_gog_style_name_from_obj (state, series->data);

			odf_write_attached_axes (state, series->data);

			gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
			g_free (str);

			odf_write_label_cell_address
				(state, gog_series_get_name (GOG_SERIES (series->data)));

			if (NULL != class)
				gsf_xml_out_add_cstr_unchecked (state->xml, CHART "class", class);

			if (NULL != cat && odf_write_data_element (state, cat, &pp, CHART "domain",
								   TABLE "cell-range-address",
								   GNMSTYLE "cell-range-expression"))
				gsf_xml_out_end_element (state->xml); /* </chart:domain> */

			role = gog_object_find_role_by_name
				(GOG_OBJECT (series->data), "Regression curve");
			if (role != NULL)
				odf_write_regression_curve (state, role, GOG_OBJECT (series->data), &pp);

			role = gog_object_find_role_by_name
				(GOG_OBJECT (series->data), "Trend line");
			if (role != NULL)
				odf_write_regression_curve (state, role, GOG_OBJECT (series->data), &pp);

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
						(state, GOG_OBJECT (l->data));
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
						     "Horizontal drop lines");
				odf_write_drop_line (state, GOG_OBJECT (series->data),
						     "Vertical drop lines");
				odf_write_drop_line (state, GOG_OBJECT (series->data),
						     "Drop lines");
				odf_write_series_lines (state, GOG_OBJECT (series->data));
			}
			gsf_xml_out_end_element (state->xml); /* </chart:series> */
		}
	}
}

static void
odf_write_box_series (GnmOOExport *state, GSList const *series, char const* class)
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
				str = odf_get_gog_style_name_from_obj (state, series->data);
				gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
				g_free (str);
				odf_write_label_cell_address
					(state, gog_series_get_name (GOG_SERIES (series->data)));
				if (NULL != class)
					gsf_xml_out_add_cstr_unchecked (state->xml, CHART "class", class);
				gsf_xml_out_end_element (state->xml); /* </chart:series> */
			}
		}
	}
}

static void
odf_write_gantt_series (GnmOOExport *state, GSList const *series, char const* class)
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
				str = odf_get_gog_style_name_from_obj (state, series->data);
				gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
				g_free (str);

				if (NULL != class)
					gsf_xml_out_add_cstr_unchecked (state->xml, CHART "class", class);

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
				str = odf_get_gog_style_name_from_obj (state, series->data);
				gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
				g_free (str);
				gsf_xml_out_end_element (state->xml); /* </chart:series> */
			}
		}
	}
}

static void
odf_write_bubble_series (GnmOOExport *state, GSList const *orig_series, char const* class)
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
				str = odf_get_gog_style_name_from_obj (state, series->data);
				gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
				g_free (str);

				if (NULL != class)
					gsf_xml_out_add_cstr_unchecked (state->xml, CHART "class", class);

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
odf_write_min_max_series (GnmOOExport *state, GSList const *orig_series, char const* class)
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
					str = odf_get_gog_style_name_from_obj (state, series->data);
					gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
					g_free (str);
					break;
				}
			}
			if (NULL != class)
				gsf_xml_out_add_cstr_unchecked (state->xml, CHART "class", class);
		}
		gsf_xml_out_end_element (state->xml); /* </chart:series> */
	}
}


static void
odf_write_fill_type (GnmOOExport *state,
		     GogObject const *series)
{
	gchar *type_str = NULL;

	if (state->with_extension && gnm_object_has_readable_prop (series, "fill-type", G_TYPE_STRING, &type_str)) {
		gsf_xml_out_add_cstr (state->xml, GNMSTYLE "fill-type", type_str);
		g_free (type_str);
	}
}

static void
odf_write_interpolation_attribute (GnmOOExport *state,
				   G_GNUC_UNUSED GOStyle const *style,
				   GogObject const *series)
{
	gchar *interpolation = NULL;

	g_object_get (G_OBJECT (series),
		      "interpolation", &interpolation,
		      NULL);

	if (interpolation != NULL) {
		if (0 == strcmp (interpolation, "linear"))
			gsf_xml_out_add_cstr
				(state->xml, CHART "interpolation", "none");
		else if (0 == strcmp (interpolation, "spline"))
			gsf_xml_out_add_cstr
				(state->xml, CHART "interpolation",
				 "cubic-spline");
		else if (0 == strcmp (interpolation, "odf-spline"))
			/* this one is really compatible with ODF */
			gsf_xml_out_add_cstr
				(state->xml, CHART "interpolation",
				 "cubic-spline");
		else if (state->with_extension) {
			char *tag = g_strdup_printf ("gnm:%s", interpolation);
			gsf_xml_out_add_cstr
				(state->xml, GNMSTYLE "interpolation", tag);
			g_free (tag);
		} else
			gsf_xml_out_add_cstr
				(state->xml, CHART "interpolation", "none");
	}

	if (state->with_extension) {
		gboolean skip_invalid = TRUE;

		if (!gnm_object_has_readable_prop (series,
						  "interpolation-skip-invalid",
						  G_TYPE_BOOLEAN,
						  &skip_invalid) ||
		    !skip_invalid)
			odf_add_bool (state->xml,
				      GNMSTYLE "interpolation-skip-invalid",
				      FALSE);
	}

	g_free (interpolation);
}

static int
odf_scale_initial_angle (double angle)
{
	angle = 90 - angle;
	while (angle < 0)
		angle += 360;
	angle = gnm_fake_round (angle);

	return (((int) angle) % 360);
}

static void
odf_write_plot_style (GnmOOExport *state, GogObject const *plot)
{
	gchar const *plot_type = G_OBJECT_TYPE_NAME (plot);
	gchar *type_str = NULL;
	double default_separation = 0.;
	double d;

	odf_add_bool (state->xml, CHART "auto-size", TRUE);

	if (gnm_object_has_readable_prop (plot, "type",
					  G_TYPE_STRING, &type_str)) {
		if (type_str != NULL) {
			odf_add_bool (state->xml, CHART "stacked",
				      (0== strcmp (type_str, "stacked")));
			odf_add_bool (state->xml, CHART "percentage",
				      (0== strcmp (type_str, "as_percentage")));
			g_free (type_str);
		}
	}

	if (gnm_object_has_readable_prop (plot, "default-separation",
					  G_TYPE_DOUBLE, &default_separation)) {
		if (0 == strcmp ("GogRingPlot", plot_type)) {
			if (state->with_extension)
				odf_add_percent (state->xml,
						 GNMSTYLE "default-separation",
						 default_separation);
		} else
			gsf_xml_out_add_int (state->xml,
					     CHART "pie-offset",
					     round (default_separation * 100));
	}

	/* Note: horizontal refers to the bars and vertical to  the x-axis */
	odf_write_plot_style_bool (state->xml, plot,
				   "horizontal", CHART "vertical");

	odf_write_plot_style_bool (state->xml, plot,
				   "vertical", CHART "vertical");

	odf_write_plot_style_from_bool
		(state->xml, plot,
		 "default-style-has-markers", CHART "symbol-type",
		 "automatic", "none");

	odf_write_plot_style_int (state->xml, plot,
				  "gap-percentage", CHART "gap-width");

	odf_write_plot_style_int (state->xml, plot,
				  "overlap-percentage", CHART "overlap");

	odf_write_plot_style_double_percent (state->xml, plot,
					     "center-size",
					     CHART "hole-size");

	if (gnm_object_has_readable_prop (plot, "initial-angle", G_TYPE_DOUBLE, &d))
		gsf_xml_out_add_int (state->xml, CHART "angle-offset", odf_scale_initial_angle (d));

	if (gnm_object_has_readable_prop (plot, "interpolation",
					  G_TYPE_NONE, NULL))
		odf_write_interpolation_attribute (state, NULL, plot);

	if (0 == strcmp ( "GogXYZSurfacePlot", plot_type) ||
	    0 == strcmp ( "GogSurfacePlot", plot_type) ||
	    0 == strcmp ( "XLSurfacePlot", plot_type))
		odf_add_bool (state->xml, CHART "three-dimensional", TRUE);
	else
		odf_add_bool (state->xml, CHART "three-dimensional", FALSE);

	odf_write_plot_style_bool (state->xml, plot, "default-style-has-lines", CHART "lines");

	if (state->with_extension) {
		if (0 == strcmp ( "XLSurfacePlot", plot_type))
			odf_add_bool (state->xml, GNMSTYLE "multi-series",
				      TRUE);
		odf_write_plot_style_bool (state->xml, plot,
					   "outliers", GNMSTYLE "outliers");

		odf_write_plot_style_double (state->xml, plot,
					     "radius-ratio", GNMSTYLE
					     "radius-ratio");

		odf_write_plot_style_bool (state->xml, plot,
					   "vary-style-by-element",
					   GNMSTYLE "vary-style-by-element");

		odf_write_plot_style_bool (state->xml, plot,
				   "show-negatives",
					   GNMSTYLE "show-negatives");
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
odf_add_expr (GnmOOExport *state, GogObject const *obj, gint dim,
	      char const *attribute, char const *c_attribute)
{
		GnmParsePos pp;
		GOData const *bd;
		parse_pos_init (&pp, WORKBOOK (state->wb), NULL, 0,0 );
		bd = gog_dataset_get_dim (GOG_DATASET (obj), dim);
		if (bd != NULL)
			odf_write_data_attribute
				(state, bd, &pp, attribute, c_attribute);
}

static void
odf_write_axis_position (GnmOOExport *state, G_GNUC_UNUSED GOStyle const *style,
			 GogObject const *axis)
{
	char *pos_str = NULL;
	if (gnm_object_has_readable_prop (axis, "pos-str",
					  G_TYPE_STRING, &pos_str)) {
		if (0 == strcmp (pos_str, "low"))
			gsf_xml_out_add_cstr (state->xml, CHART "axis-position", "start");
		else if (0 == strcmp (pos_str, "high"))
			gsf_xml_out_add_cstr (state->xml, CHART "axis-position", "end");
		else if (0 == strcmp (pos_str, "cross")) {
			GnmParsePos pp;
			GOData const *bd;
			parse_pos_init (&pp, WORKBOOK (state->wb), NULL, 0, 0);
			bd = gog_dataset_get_dim (GOG_DATASET (axis), 4);
			if (bd != NULL)
				odf_write_data_attribute (state, bd, &pp,
							  GNMSTYLE "axis-position-expression",
							  CHART "axis-position");
			else
				gsf_xml_out_add_cstr (state->xml, CHART "axis-position", "0");
		}
		g_free (pos_str);
	}
}

static void
odf_write_axisline_style (GnmOOExport *state, GOStyle const *style,
		      GogObject const *axis)
{
	odf_write_axis_position (state, style, axis);

	odf_write_plot_style_bool
		(state->xml, axis, "major-tick-in", CHART "tick-marks-major-inner");
	odf_write_plot_style_bool
		(state->xml, axis, "major-tick-out", CHART "tick-marks-major-outer");
	odf_write_plot_style_bool
		(state->xml, axis, "minor-tick-in", CHART "tick-marks-minor-inner");
	odf_write_plot_style_bool
		(state->xml, axis, "minor-tick-out", CHART "tick-marks-minor-outer");
	odf_write_plot_style_bool
		(state->xml, axis, "major-tick-labeled", CHART "display-label");
}

static void
odf_write_axis_style (GnmOOExport *state, GOStyle const *style,
		      GogObject const *axis)
{
	double tmp;
	GOData const *interval;
	gboolean user_defined;
	gboolean logarithmic = FALSE;
	char *map_name_str = NULL;

	if (gnm_object_has_readable_prop (axis, "map-name",
					  G_TYPE_STRING, &map_name_str)) {
		logarithmic = (0 != strcmp (map_name_str, "Linear"));
		odf_add_bool (state->xml, CHART "logarithmic", logarithmic);
		g_free (map_name_str);
	}

	tmp = gog_axis_get_entry
		(GOG_AXIS (axis), GOG_AXIS_ELEM_MIN, &user_defined);
	if (user_defined) {
		go_xml_out_add_double (state->xml, CHART "minimum", tmp);
		if (state->with_extension)
			odf_add_expr (state, GOG_OBJECT (axis), 0,
				      GNMSTYLE "chart-minimum-expression", NULL);
	}
	tmp = gog_axis_get_entry
		(GOG_AXIS (axis), GOG_AXIS_ELEM_MAX, &user_defined);
	if (user_defined) {
		go_xml_out_add_double (state->xml, CHART "maximum", tmp);
		if (state->with_extension)
			odf_add_expr (state, GOG_OBJECT (axis), 1,
				      GNMSTYLE "chart-maximum-expression", NULL);
	}

	interval = gog_dataset_get_dim (GOG_DATASET(axis),2);
	if (interval != NULL) {
		GnmExprTop const *texpr
			= gnm_go_data_get_expr (interval);
		if (texpr != NULL &&
		    GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_CONSTANT) {
			double val = value_get_as_float (texpr->expr->constant.value);
			go_xml_out_add_double (state->xml, CHART "interval-major", val);

			interval = gog_dataset_get_dim (GOG_DATASET(axis),3);
			if (interval != NULL) {
				texpr = gnm_go_data_get_expr (interval);
				if (texpr != NULL &&
				    GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_CONSTANT) {
					double val_minor = value_get_as_float
						(texpr->expr->constant.value);
					if (val_minor > 0) {
						if (logarithmic)
							val_minor = gnm_floor(val_minor + 1.5);
						else
							val_minor = gnm_round(val / val_minor);
						gsf_xml_out_add_float
							(state->xml, CHART "interval-minor-divisor",
							 val_minor, 0);
					}
				}
			}
		}
	}
	if (state->odf_version > 101)
		odf_write_plot_style_bool
			(state->xml, axis,
			 "invert-axis", CHART "reverse-direction");
	else if (state->with_extension)
		odf_write_plot_style_bool
			(state->xml, axis,
			 "invert-axis", GNMSTYLE "reverse-direction");

	odf_write_axisline_style (state, style, axis);
}

static void
odf_write_generic_axis_style (GnmOOExport *state, char const *style_label)
{
	odf_start_style (state->xml, style_label, "chart");
	gsf_xml_out_start_element (state->xml, STYLE "chart-properties");

	gsf_xml_out_add_cstr (state->xml, CHART "axis-position", "start");
	odf_add_bool (state->xml, CHART "display-label", TRUE);

	if (state->odf_version > 101)
		odf_add_bool (state->xml, CHART "reverse-direction", TRUE);
	gsf_xml_out_end_element (state->xml); /* </style:chart-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */
}

static void
odf_write_one_axis_grid (GnmOOExport *state, GogObject const *axis,
			 char const *role, char const *class)
{
	GogObject const *grid;

	grid = gog_object_get_child_by_name (axis, role);
	if (grid) {
		char *style = odf_get_gog_style_name_from_obj (state, GOG_OBJECT (grid));

		gsf_xml_out_start_element (state->xml, CHART "grid");
		gsf_xml_out_add_cstr (state->xml, CHART "style-name", style);
		gsf_xml_out_add_cstr (state->xml, CHART "class", class);
		gsf_xml_out_end_element (state->xml); /* </chart:grid> */

		g_free (style);
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
odf_write_axislines (GnmOOExport *state, GogObject const *axis)
{
	g_return_if_fail (axis != NULL);

	if (state->with_extension) {
		GogObjectRole const *role;
		role = gog_object_find_role_by_name (axis, "AxisLine");
		if (role != NULL) {
			GSList *l, *lines = gog_object_get_children (axis, role);
			l = lines;
			while (l != NULL && l->data != NULL) {
				char *name = odf_get_gog_style_name_from_obj (state, GOG_OBJECT (l->data));
				gsf_xml_out_start_element (state->xml, GNMSTYLE "axisline");
				if (name != NULL)
					gsf_xml_out_add_cstr (state->xml, CHART "style-name", name);
				gsf_xml_out_end_element (state->xml); /* </gnm:axisline> */
				g_free (name);
				l = l->next;
			}
			g_slist_free (lines);
		}

	}
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
				GnmParsePos ppos;
				char *formula;
				char *name;
				gboolean pp = TRUE;
				GnmValue const *v;

				g_object_get (G_OBJECT (state->xml), "pretty-print", &pp, NULL);

				gsf_xml_out_start_element (state->xml, id);

				odf_write_gog_position (state, title);
				odf_write_gog_position_pts (state, title);

				name = odf_get_gog_style_name_from_obj (state, title);

				if (name != NULL) {
					gsf_xml_out_add_cstr (state->xml, CHART "style-name",
							      name);
					g_free (name);
				}

				parse_pos_init (&ppos, WORKBOOK (state->wb), NULL, 0,0 );
				formula = gnm_expr_top_as_string (texpr, &ppos, state->conv);

				if (gnm_expr_top_is_rangeref (texpr)) {
					char *f = odf_strip_brackets (formula);
					gsf_xml_out_add_cstr (state->xml,
							      TABLE "cell-range", f);
				} else if (allow_content &&
					   (v = gnm_expr_top_get_constant (texpr)) &&
					   VALUE_IS_STRING (v)) {
					gboolean white_written = TRUE;
					char const *str;
					GogText *text;
					g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
					gsf_xml_out_start_element (state->xml, TEXT "p");
					str = value_peek_string (v);
					if (GOG_IS_TEXT (title) &&
					    (text = GOG_TEXT (title))->allow_markup) {
						PangoAttrList *attr_list = NULL;
						char *text_clean = NULL;
						if (pango_parse_markup (str, -1, 0,
									&attr_list,
									&text_clean, NULL, NULL)) {
							odf_new_markup (state, attr_list, text_clean);
							g_free (text_clean);
							pango_attr_list_unref (attr_list);
						} else
							odf_add_chars (state, str,strlen (str),
								       &white_written);
					} else
						odf_add_chars (state, str,strlen (str),
							       &white_written);
					gsf_xml_out_end_element (state->xml); /* </text:p> */
					g_object_set (G_OBJECT (state->xml), "pretty-print", pp, NULL);
				} else {
					gboolean white_written = TRUE;
					if (state->with_extension)
						gsf_xml_out_add_cstr (state->xml,
								      GNMSTYLE "expression",
								      formula);
					if (allow_content) {
						g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
						gsf_xml_out_start_element
							(state->xml, TEXT "p");
						odf_add_chars (state, formula,
							       strlen (formula),
							       &white_written);
						gsf_xml_out_end_element (state->xml);
						/* </text:p> */
						g_object_set (G_OBJECT (state->xml), "pretty-print", pp, NULL);
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

	if (old->fill.gradient.brightness >= 0)
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
	return !go_image_differ (old, new);
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

static char *
odf_get_border_info (G_GNUC_UNUSED GnmOOExport *state, GOStyle const *style)
{
	if (style->line.width <= 0)
		return g_strdup ("thin");
	if (style->line.width == 1.5)
		return g_strdup ("medium");
	if (style->line.width == 3)
		return g_strdup ("thick");
	return g_strdup_printf ("%.6fpt", style->line.width);
}

static void
odf_write_gog_style_graphic (GnmOOExport *state, GOStyle const *style, gboolean with_border)
{
	char const *image_types[] =
		{"stretch", "repeat", "no-repeat"};

	if (!style)
		return;

	if (style->interesting_fields & (GO_STYLE_FILL)) {
		if (state->with_extension && style->fill.auto_type) {
			odf_add_bool (state->xml, GNMSTYLE "auto-type", TRUE);
		}
		/* We need to write our colours even for auto_type == TRUE since nobody else understands this */
		switch (style->fill.type) {
		case GO_STYLE_FILL_NONE:
			gsf_xml_out_add_cstr (state->xml, DRAW "fill", "none");
			break;
		case GO_STYLE_FILL_PATTERN:
			if (style->fill.pattern.pattern == GO_PATTERN_SOLID) {
				gsf_xml_out_add_cstr (state->xml, DRAW "fill", "solid");
				if (!style->fill.auto_back) {
					char *color = odf_go_color_to_string (style->fill.pattern.back);
					gsf_xml_out_add_cstr (state->xml, DRAW "fill-color", color);
					odf_add_percent (state->xml, DRAW "opacity",
							 odf_go_color_opacity (style->fill.pattern.back));
					g_free (color);
				}
			} else if (style->fill.pattern.pattern == GO_PATTERN_FOREGROUND_SOLID) {
				if (state->with_extension)
					odf_add_bool (state->xml, GNMSTYLE "foreground-solid", TRUE);
				gsf_xml_out_add_cstr (state->xml, DRAW "fill", "solid");
				if (!style->fill.auto_fore) {
					char *color = odf_go_color_to_string (style->fill.pattern.fore);
					gsf_xml_out_add_cstr (state->xml, DRAW "fill-color", color);
					odf_add_percent (state->xml, DRAW "opacity",
							 odf_go_color_opacity (style->fill.pattern.fore));
					g_free (color);
				}
			} else {
				gchar *hatch = odf_get_pattern_name (state, style);
				gsf_xml_out_add_cstr (state->xml, DRAW "fill", "hatch");
				gsf_xml_out_add_cstr (state->xml, DRAW "fill-hatch-name",
						      hatch);
				if (!style->fill.auto_back) {
					char *color = odf_go_color_to_string (style->fill.pattern.back);
					gsf_xml_out_add_cstr (state->xml, DRAW "fill-color", color);
					odf_add_percent (state->xml, DRAW "opacity",
							 odf_go_color_opacity (style->fill.pattern.back));
					g_free (color);
				}
				g_free (hatch);
				odf_add_bool (state->xml, DRAW "fill-hatch-solid", TRUE);
				if (state->with_extension)
					gsf_xml_out_add_int
						(state->xml,
						 GNMSTYLE "pattern",
						 style->fill.pattern.pattern);
			}
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
			if (style->fill.image.type < G_N_ELEMENTS (image_types))
				gsf_xml_out_add_cstr (state->xml, STYLE "repeat",
						      image_types [style->fill.image.type]);
			else g_warning ("Unexpected GOImageType value");
			break;
		}
		}
	}

	if (style->interesting_fields & (GO_STYLE_LINE | GO_STYLE_OUTLINE | GO_STYLE_MARKER)) {
		GOLineDashType dash_type = style->line.dash_type;
		gboolean has_line = go_style_is_line_visible (style);
		gboolean is_auto;
		GOColor color;

		if (!has_line)
			gsf_xml_out_add_cstr (state->xml,
					      DRAW "stroke", "none");
		else if (dash_type == GO_LINE_SOLID)
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
		if (style->line.auto_dash && state->with_extension)
			odf_add_bool (state->xml, GNMSTYLE "auto-dash", TRUE);

		if (style->line.auto_width && state->with_extension)
			odf_add_bool (state->xml, GNMSTYLE "auto-width", TRUE);
		else if (style->line.width == 0) {
			odf_add_pt (state->xml, SVG "stroke-width", 1.);
			if (state->with_extension)
				odf_add_pt (state->xml, GNMSTYLE "stroke-width", 0.);
		} else if (style->line.width > 0)
			odf_add_pt (state->xml, SVG "stroke-width",
				    style->line.width);

		/*
		 * ods doesn't have separate colours for the marker, so use
		 * the marker colour if we don't have a line.
		 */
		is_auto = style->line.auto_color;
		color = style->line.color;
		if (!has_line && (style->interesting_fields & GO_STYLE_MARKER)) {
			is_auto = style->marker.auto_fill_color;
			color = go_marker_get_fill_color (style->marker.mark);
		}

		if (!is_auto) {
			char *s = odf_go_color_to_string (color);
			gsf_xml_out_add_cstr (state->xml, SVG "stroke-color", s);
			g_free (s);

			if (state->with_extension) {
				if (odf_go_color_has_opacity (color))
					odf_add_percent (state->xml, GNMSTYLE "stroke-color-opacity",
							 odf_go_color_opacity (color));

				GOColor c = go_marker_get_outline_color (style->marker.mark);
				s = odf_go_color_to_string (c);
				gsf_xml_out_add_cstr (state->xml, GNMSTYLE "marker-outline-colour", s);
				g_free (s);
				if (odf_go_color_has_opacity (c))
					odf_add_percent (state->xml, GNMSTYLE "marker-outline-colour-opacity",
							 odf_go_color_opacity (c));

				c = go_marker_get_fill_color (style->marker.mark);
				s = odf_go_color_to_string (c);
				gsf_xml_out_add_cstr (state->xml, GNMSTYLE "marker-fill-colour", s);
				if (odf_go_color_has_opacity (c))
					odf_add_percent (state->xml, GNMSTYLE "marker-fill-colour-opacity",
							 odf_go_color_opacity (c));
				g_free (s);
			}
		} else if (state->with_extension)
			odf_add_bool (state->xml, GNMSTYLE "auto-color", style->fill.auto_fore);
		if (state->with_extension && (style->interesting_fields & GO_STYLE_MARKER)) {
			odf_add_bool (state->xml, GNMSTYLE "auto-marker-outline-colour",
				      style->marker.auto_outline_color);
			odf_add_bool (state->xml, GNMSTYLE "auto-marker-fill-colour",
				      style->marker.auto_fill_color);
		}
	} else {
		gsf_xml_out_add_cstr (state->xml, DRAW "stroke", "none");
	}

	if (with_border && go_style_is_outline_visible (style)) {
		char *border = odf_get_border_info (state, style);
		if (strlen (border) > 0)
			gsf_xml_out_add_cstr (state->xml, FOSTYLE "border", border);
		g_free (border);
	}
}

static void
odf_write_gog_style_text (GnmOOExport *state, GOStyle const *style)
{
	if (style != NULL) {
		PangoFontDescription const *desc = style->font.font->desc;
		PangoFontMask mask = pango_font_description_get_set_fields (desc);

		if (!style->text_layout.auto_angle) {
			int val = style->text_layout.angle;
			odf_add_angle (state->xml, STYLE "text-rotation-angle", val);
		}

		if (!style->font.auto_color) {
			char *color = odf_go_color_to_string (style->font.color);
			gsf_xml_out_add_cstr (state->xml, FOSTYLE "color", color);
			g_free (color);
		}

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
		if (mask & PANGO_FONT_MASK_WEIGHT)
			odf_add_font_weight (state,
					     pango_font_description_get_weight (desc));

		if ((mask & PANGO_FONT_MASK_STRETCH) && state->with_extension)
			gsf_xml_out_add_int (state->xml, GNMSTYLE "font-stretch-pango",
					     pango_font_description_get_stretch (desc));
		if ((mask & PANGO_FONT_MASK_GRAVITY) && state->with_extension)
			gsf_xml_out_add_int (state->xml, GNMSTYLE "font-gravity-pango",
					     pango_font_description_get_gravity (desc));

		if (state->with_extension)
			odf_add_bool (state->xml, GNMSTYLE "auto-font",
				      style->font.auto_font);
	}
}

static void
odf_write_gog_style_chart (GnmOOExport *state, GOStyle const *style, GogObject const *obj)
{
	gchar const *type = G_OBJECT_TYPE_NAME (G_OBJECT (obj));
	void (*func) (GnmOOExport *state, GOStyle const *style, GogObject const *obj);

	if (GOG_IS_PLOT (obj))
		odf_write_plot_style (state, obj);

	if (GOG_IS_AXIS (obj)) {
		GOFormat *fmt = gog_axis_get_format (GOG_AXIS (obj));
		odf_add_bool (state->xml, CHART "link-data-style-to-source", fmt == NULL);
	}

	odf_write_fill_type (state, obj);

	func = g_hash_table_lookup (state->chart_props_hash, type);
	if (func != NULL)
		func (state, style, obj);

	if (!style)
		return;

	if (style->interesting_fields & (GO_STYLE_LINE | GO_STYLE_OUTLINE)) {
		odf_add_bool (state->xml,
			      CHART "lines",
			      go_style_is_line_visible (style));
	}

	if (style->interesting_fields & GO_STYLE_MARKER) {
		GOMarker const *marker = go_style_get_marker (style);
		const char *symbol_type = NULL;

		if (style->marker.auto_shape) {
			if (GOG_IS_SERIES (obj)) {
				GogPlot *plot =	gog_series_get_plot (GOG_SERIES (obj));
				gboolean has_marker = TRUE;

				if (gnm_object_has_readable_prop
				    (plot, "default-style-has-markers",
				     G_TYPE_BOOLEAN, &has_marker)) {
					if (has_marker)
						symbol_type = "automatic";
				} else
					symbol_type = "automatic";
			} else
				symbol_type = "automatic";
		} else {
			GOMarkerShape m = go_marker_get_shape (marker);

			if (m != GO_MARKER_NONE) {
				symbol_type = "named-symbol";

				gsf_xml_out_add_cstr
					(state->xml, CHART "symbol-name", odf_get_marker (m));
			}
		}

		if (symbol_type) {
			int size = go_marker_get_size (marker);
			odf_add_pt (state->xml, CHART "symbol-width", size);
			odf_add_pt (state->xml, CHART "symbol-height", size);
		} else
			symbol_type = "none";

		gsf_xml_out_add_cstr (state->xml, CHART "symbol-type", symbol_type);
	}
}

static void
odf_write_gog_style (GnmOOExport *state, GOStyle const *style,
		     GogObject const *obj)
{
	char *name = odf_get_gog_style_name (state, style, obj);
	if (name != NULL) {
		odf_start_style (state->xml, name, "chart");

		if (GOG_IS_AXIS (obj)) {
			GOFormat *fmt = gog_axis_get_format (GOG_AXIS (obj));
			if (fmt) {
				char const *name = xl_find_format (state, fmt);
				gsf_xml_out_add_cstr (state->xml, STYLE "data-style-name", name);
			}
		}

		gsf_xml_out_start_element (state->xml, STYLE "chart-properties");
		odf_write_gog_style_chart (state, style, obj);
		gsf_xml_out_end_element (state->xml); /* </style:chart-properties> */

		gsf_xml_out_start_element (state->xml, STYLE "graphic-properties");
		odf_write_gog_style_graphic (state, style, FALSE);
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
	GSList *children;
	GOStyle *style = NULL;

	if (gnm_object_has_readable_prop (obj, "style", G_TYPE_NONE, &style)) {
		odf_write_gog_style (state, style, obj);
		if (style != NULL)
			g_object_unref (style);
	} else
		odf_write_gog_style (state, NULL, obj);

	children = gog_object_get_children (obj, NULL);
	g_slist_foreach (children, (GFunc) odf_write_gog_styles, state);
	g_slist_free (children);
}

static void
odf_write_axis_categories (GnmOOExport *state, GSList const *series, GogMSDimType dim)
{
	if (series != NULL && series->data != NULL) {
		GOData const *cat = gog_dataset_get_dim (GOG_DATASET (series->data), dim);
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
odf_write_axis_full (GnmOOExport *state,
		     GogObject const *chart,
		     char const *axis_role,
		     char const *dimension,
		     G_GNUC_UNUSED odf_chart_type_t gtype,
		     GSList const *series,
		     gboolean include_cats,
		     GogMSDimType dim)
{
	GSList *children = NULL, *l;
	GString *str;

	if (axis_role == NULL)
		return;

	str = g_string_new (NULL);
	children = gog_object_get_children (chart, gog_object_find_role_by_name (chart, axis_role));

	for (l = children; l != NULL; l = l->next) {
		GogObject const *axis = l->data;
		if (axis != NULL) {
			int id = gog_object_get_id (GOG_OBJECT (axis));
			char *name;

			gsf_xml_out_start_element (state->xml, CHART "axis");
			gsf_xml_out_add_cstr (state->xml, CHART "dimension", dimension);
			if (state->with_extension)
				gsf_xml_out_add_int (state->xml, GNMSTYLE "id", id);
			g_string_truncate (str, 0);
			g_string_append_printf (str, "%s-%i", axis_role, id);
			gsf_xml_out_add_cstr_unchecked (state->xml, CHART "name", str->str);
			name = odf_get_gog_style_name_from_obj (state, GOG_OBJECT (axis));
			if (name != NULL)
				gsf_xml_out_add_cstr (state->xml, CHART "style-name", name);
			g_free (name);
       			if (state->with_extension && 0 == strcmp (axis_role,"Pseudo-3D-Axis")) {
				char *color_map_name = NULL;
				g_object_get (G_OBJECT (axis), "color-map-name", &color_map_name, NULL);
				if (color_map_name) {
					gsf_xml_out_add_cstr (state->xml, GNMSTYLE "color-map-name", color_map_name);
					g_free (color_map_name);
				}
			}
			odf_write_label (state, axis);
			if (include_cats)
				odf_write_axis_categories (state, series, dim);
			odf_write_axis_grid (state, axis);
			odf_write_axislines (state, axis);
			gsf_xml_out_end_element (state->xml); /* </chart:axis> */
		}
	}
	g_slist_free (children);
	g_string_free (str, TRUE);
}

static void
odf_write_axis (GnmOOExport *state,
		GogObject const *chart,
		char const *axis_role,
		char const *dimension,
		odf_chart_type_t gtype,
		GogMSDimType dim,
		GSList const *series)
{
	odf_write_axis_full (state, chart, axis_role, dimension, gtype, series, TRUE, dim);
}

static void
odf_write_axis_no_cats (GnmOOExport *state,
			GogObject const *chart,
			char const *axis_role,
			char const *dimension,
			odf_chart_type_t gtype,
			GogMSDimType dim,
			GSList const *series)
{
	odf_write_axis_full (state, chart, axis_role, dimension, gtype, series, FALSE, dim);
}

static void
odf_write_pie_axis (GnmOOExport *state,
		    G_GNUC_UNUSED GogObject const *chart,
		    G_GNUC_UNUSED char const *axis_role,
		    char const *dimension,
		    G_GNUC_UNUSED odf_chart_type_t gtype,
		    GogMSDimType dim,
		    GSList const *series)
{
	gsf_xml_out_start_element (state->xml, CHART "axis");
	gsf_xml_out_add_cstr (state->xml, CHART "dimension", dimension);
	gsf_xml_out_add_cstr (state->xml, CHART "style-name", "pie-axis");
	odf_write_axis_categories (state, series, dim);
	gsf_xml_out_end_element (state->xml); /* </chart:axis> */
}


static void
odf_write_plot (GnmOOExport *state, SheetObject *so, GogObject const *graph,
		GogObject const *chart, GogObject const *plot, GSList *other_plots)
{
	char const *plot_type = G_OBJECT_TYPE_NAME (plot);
	SheetObjectAnchor const *anchor = sheet_object_get_anchor (so);
	double res_pts[4] = {0.,0.,0.,0.};
	GSList const *series, *l;
	GogObject const *wall = gog_object_get_child_by_name (chart, "Backplane");
	GogObject const *legend = gog_object_get_child_by_name (chart, "Legend");
	GogObject const *color_scale = gog_object_get_child_by_name (chart, "Color-Scale");
	GogObjectRole const *trole = gog_object_find_role_by_name (graph, "Title");
	GSList *titles = gog_object_get_children (graph, trole);
	GogObjectRole const *trole2 = gog_object_find_role_by_name (chart, "Title");
	GSList *subtitles = gog_object_get_children (chart, trole2);
	char *name;
	GOStyle *style = NULL;

	static struct {
		char const * type;
		char const *odf_plot_type;
		odf_chart_type_t gtype;
		double pad;
		char const * x_axis_name;
		char const * y_axis_name;
		char const * z_axis_name;
		GogMSDimType x_dim;
		GogMSDimType y_dim;
		GogMSDimType z_dim;
		void (*odf_write_series)       (GnmOOExport *state,
						GSList const *series,
						char const* class);
		void (*odf_write_x_axis) (GnmOOExport *state,
					  GogObject const *chart,
					  char const *axis_role,
					  char const *dimension,
					  odf_chart_type_t gtype,
					  GogMSDimType dim,
					  GSList const *series);
		void (*odf_write_y_axis) (GnmOOExport *state,
					  GogObject const *chart,
					  char const *axis_role,
					  char const *dimension,
					  odf_chart_type_t gtype,
					  GogMSDimType dim,
					  GSList const *series);
		void (*odf_write_z_axis) (GnmOOExport *state,
					  GogObject const *chart,
					  char const *axis_role,
					  char const *dimension,
					  odf_chart_type_t gtype,
					  GogMSDimType dim,
					  GSList const *series);
	} *this_plot, *this_second_plot, plots[] = {
		{ "GogColPlot", CHART "bar", ODF_BARCOL,
		  20., "X-Axis", "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis_no_cats, odf_write_axis},
		{ "GogBarPlot", CHART "bar", ODF_BARCOL,
		  20., "Y-Axis", "X-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis_no_cats, odf_write_axis},
		{ "GogLinePlot", CHART "line", ODF_LINE,
		  20., "X-Axis", "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogPolarPlot", GNMSTYLE "polar", ODF_POLAR,
		  20., "Circular-Axis", "Radial-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogAreaPlot", CHART "area", ODF_AREA,
		  20., "X-Axis", "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogDropBarPlot", CHART "gantt", ODF_DROPBAR,
		  20., "X-Axis", "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_gantt_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogMinMaxPlot", CHART "stock", ODF_MINMAX,
		  10., "X-Axis", "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_min_max_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogPiePlot", CHART "circle", ODF_CIRCLE,
		  5., NULL, "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_standard_series,
		  NULL, odf_write_pie_axis, NULL},
		{ "GogRadarPlot", CHART "radar", ODF_RADAR,
		  10., "Circular-Axis", "Radial-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogRadarAreaPlot", CHART "filled-radar", ODF_RADARAREA,
		  10., "X-Axis", "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogRingPlot", CHART "ring", ODF_RING,
		  10., NULL, "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_standard_series,
		  NULL, odf_write_pie_axis, NULL},
		{ "GogXYPlot", CHART "scatter", ODF_SCATTER,
		  20., "X-Axis", "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogContourPlot", CHART "surface", ODF_SURF,
		  20., "X-Axis", "Y-Axis", "Pseudo-3D-Axis",
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_bubble_series,
		  odf_write_axis, odf_write_axis, odf_write_axis_no_cats},
		{ "GogXYZContourPlot", GNMSTYLE "xyz-surface", ODF_XYZ_SURF,
		  20., "X-Axis", "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_bubble_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogXYZSurfacePlot", GNMSTYLE "xyz-surface", ODF_XYZ_GNM_SURF,
		  20., "X-Axis", "Y-Axis", "Z-Axis",
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_bubble_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogSurfacePlot", CHART "surface", ODF_GNM_SURF,
		  20., "X-Axis", "Y-Axis", "Z-Axis",
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_VALUES, GOG_MS_DIM_LABELS,
		  odf_write_bubble_series,
		  odf_write_axis, odf_write_axis, odf_write_axis_no_cats},
		{ "GogBubblePlot", CHART "bubble", ODF_BUBBLE,
		  20., "X-Axis", "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_bubble_series,
		  odf_write_axis_no_cats, odf_write_axis_no_cats, odf_write_axis_no_cats},
		{ "GogXYColorPlot", GNMSTYLE "scatter-color", ODF_SCATTER_COLOUR,
		  20., "X-Axis", "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_bubble_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "XLSurfacePlot", CHART "surface", ODF_GNM_SURF,
		  20., "X-Axis", "Y-Axis", "Z-Axis",
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ "GogBoxPlot", GNMSTYLE "box", ODF_GNM_BOX,
		  20., "X-Axis", "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_box_series,
		  odf_write_axis, odf_write_axis, odf_write_axis},
		{ NULL, NULL, 0,
		  20., "X-Axis", "Y-Axis", NULL,
		  GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS, GOG_MS_DIM_LABELS,
		  odf_write_standard_series,
		  odf_write_axis, odf_write_axis, odf_write_axis}
	};

	if (0 == strcmp ("GogBarColPlot", plot_type)) {
		gboolean b;

		if (gnm_object_has_readable_prop (plot, "horizontal",
						  G_TYPE_BOOLEAN, &b) &&
		    b)
			plot_type = "GogBarPlot";
		else
			plot_type = "GogColPlot";
	}

	for (this_plot = &plots[0]; this_plot->type != NULL; this_plot++)
		if (0 == strcmp (plot_type, this_plot->type))
			break;

	if (this_plot->type == NULL) {
		g_printerr ("Encountered unknown chart type %s\n", plot_type);
		this_plot = &plots[0];
	}

	series = gog_plot_get_series (GOG_PLOT (plot));

	gsf_xml_out_start_element (state->xml, OFFICE "automatic-styles");
	odf_write_character_styles (state);

	odf_write_generic_axis_style (state, "pie-axis");

	odf_start_style (state->xml, "plotstyle", "chart");
	gsf_xml_out_start_element (state->xml, STYLE "chart-properties");
	odf_add_bool (state->xml, CHART "auto-size", TRUE);
	gsf_xml_out_end_element (state->xml); /* </style:chart-properties> */
	g_object_get (G_OBJECT (chart), "style", &style, NULL);
	if (style) {
		gsf_xml_out_start_element (state->xml, STYLE "graphic-properties");
		odf_write_gog_style_graphic (state, style, TRUE);
		gsf_xml_out_end_element (state->xml); /* </style:graphic-properties> */
		g_object_unref (style);
	}
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_write_gog_styles (chart, state);

	gsf_xml_out_end_element (state->xml); /* </office:automatic-styles> */

	gsf_xml_out_start_element (state->xml, OFFICE "body");
	gsf_xml_out_start_element (state->xml, OFFICE "chart");
	gsf_xml_out_start_element (state->xml, CHART "chart");

       	sheet_object_anchor_to_pts (anchor, state->sheet, res_pts);
	odf_add_pt (state->xml, SVG "width", res_pts[2] - res_pts[0] - 2 * this_plot->pad);
	odf_add_pt (state->xml, SVG "height", res_pts[3] - res_pts[1] - 2 * this_plot->pad);

	if (state->odf_version > 101) {
		gsf_xml_out_add_cstr (state->xml, XLINK "type", "simple");
		gsf_xml_out_add_cstr (state->xml, XLINK "href", "..");
	}
	gsf_xml_out_add_cstr (state->xml, CHART "class", this_plot->odf_plot_type);
	gsf_xml_out_add_cstr (state->xml, CHART "style-name", "plotstyle");

	/* Set up title */

	if (titles != NULL) {
		GogObject const *title = titles->data;
		odf_write_title (state, title, CHART "title", TRUE);
		g_slist_free (titles);
	}
	if (subtitles != NULL) {
		GogObject const *title = subtitles->data;
		char *position;
		gboolean is_footer = FALSE;

		g_object_get (G_OBJECT (title),
			      "compass", &position,
			      NULL);
		is_footer = NULL != g_strstr_len (position, -1, "bottom");
		odf_write_title (state, title,
				 is_footer ? CHART "footer" : CHART "subtitle",
				 TRUE);
		g_slist_free (subtitles);
		g_free (position);
	}


	/* Set up legend if appropriate*/

	if (legend != NULL) {
		GogObjectPosition flags;
		char *style_name = odf_get_gog_style_name_from_obj
			(state, legend);
		GSList *ltitles = gog_object_get_children
			(legend, gog_object_find_role_by_name
			 (legend, "Title"));
		gboolean is_position_manual = FALSE;

		gsf_xml_out_start_element (state->xml, CHART "legend");
		gsf_xml_out_add_cstr (state->xml,
					      CHART "style-name",
					      style_name);
		g_free (style_name);

		odf_write_gog_position (state, legend); /* gnumeric extensions */

		g_object_get (G_OBJECT (legend),
			      "is-position-manual", &is_position_manual,
			      NULL);
		if (is_position_manual)
			odf_write_gog_position_pts (state, legend);
		else {
			flags = gog_object_get_position_flags
				(legend, GOG_POSITION_COMPASS);
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
		}

		if (ltitles != NULL) {
			GogObject const *title = ltitles->data;

			if (state->with_extension)
				odf_write_title (state, title,
						 GNMSTYLE "title", state->odf_version > 101);
			else if (state->odf_version > 101) {
				GOData const *dat =
					gog_dataset_get_dim (GOG_DATASET(title),0);

				if (dat != NULL) {
					GnmExprTop const *texpr
						= gnm_go_data_get_expr (dat);
					if (texpr != NULL &&
					    GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_CONSTANT
					    && VALUE_IS_STRING (texpr->expr->constant.value)) {
						gboolean white_written = TRUE;
						char const *str;
						gboolean pp = TRUE;
						g_object_get (G_OBJECT (state->xml), "pretty-print", &pp, NULL);
						g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
						gsf_xml_out_start_element (state->xml, TEXT "p");
						str = value_peek_string (texpr->expr->constant.value);
						odf_add_chars (state, str, strlen (str),
							       &white_written);
						gsf_xml_out_end_element (state->xml); /* </text:p> */
						g_object_set (G_OBJECT (state->xml), "pretty-print", pp, NULL);
					}
				}

			}
			g_slist_free (ltitles);
		}

		gsf_xml_out_end_element (state->xml); /* </chart:legend> */
	}

	gsf_xml_out_start_element (state->xml, CHART "plot-area");

	name = odf_get_gog_style_name_from_obj (state, plot);
	if (name != NULL) {
		gsf_xml_out_add_cstr (state->xml, CHART "style-name", name);
		g_free (name);
	}

	if (state->odf_version <= 101) {
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
			(state, chart, this_plot->z_axis_name, "z",
			 this_plot->gtype, this_plot->z_dim, series);
	if (this_plot->odf_write_y_axis)
		this_plot->odf_write_y_axis
			(state, chart, this_plot->y_axis_name, "y",
			 this_plot->gtype, this_plot->y_dim, series);
	if (this_plot->odf_write_x_axis)
		this_plot->odf_write_x_axis
			(state, chart, this_plot->x_axis_name, "x",
			 this_plot->gtype, this_plot->x_dim, series);

	if (this_plot->odf_write_series != NULL)
		this_plot->odf_write_series (state, series, NULL);

	while (other_plots) {
		GogObject const *second_plot = GOG_OBJECT (other_plots->data);
		plot_type = G_OBJECT_TYPE_NAME (second_plot);

		if (0 == strcmp ("GogBarColPlot", plot_type)) {
			gboolean b;

			if (gnm_object_has_readable_prop
			    (second_plot, "horizontal",
			     G_TYPE_BOOLEAN, &b) && b)
				plot_type = "GogBarPlot";
			else
				plot_type = "GogColPlot";
		}

		for (this_second_plot = &plots[0]; this_second_plot->type != NULL; this_second_plot++)
			if (0 == strcmp (plot_type, this_second_plot->type))
				break;

		if (this_second_plot->type == NULL) {
			g_printerr ("Encountered unknown chart type %s\n", plot_type);
			this_second_plot = &plots[0];
		}

		series = gog_plot_get_series (GOG_PLOT (second_plot));

		this_second_plot->odf_write_series (state, series, this_second_plot->odf_plot_type);
		other_plots = other_plots->next;
	}

	if (wall != NULL) {
		char *name = odf_get_gog_style_name_from_obj (state, wall);

		gsf_xml_out_start_element (state->xml, CHART "wall");
		odf_add_pt (state->xml, SVG "width", res_pts[2] - res_pts[0] - 2 * this_plot->pad);
		if (name != NULL)
			gsf_xml_out_add_cstr (state->xml, CHART "style-name", name);
		gsf_xml_out_end_element (state->xml); /* </chart:wall> */

		g_free (name);
	}
	gsf_xml_out_end_element (state->xml); /* </chart:plot_area> */

	if (color_scale != NULL && state->with_extension)
		gsf_xml_out_simple_element (state->xml, GNMSTYLE "color-scale", NULL);

	gsf_xml_out_end_element (state->xml); /* </chart:chart> */
	gsf_xml_out_end_element (state->xml); /* </office:chart> */
	gsf_xml_out_end_element (state->xml); /* </office:body> */
}


static void
odf_write_graph_content (GnmOOExport *state, GsfOutput *child, SheetObject *so, GogObject const	*chart)
{
	int i;
	GogGraph const	*graph;
	gboolean plot_written = FALSE;

	state->xml = create_new_xml_child (state, child);
	gsf_xml_out_set_doc_type (state->xml, "\n");
	gsf_xml_out_start_element (state->xml, OFFICE "document-content");

	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version",
					state->odf_version_string);

	graph = sheet_object_graph_get_gog (so);
	if (graph != NULL) {
		double pos[4];
		GogRenderer *renderer;
		GogObjectRole const *role;

		sheet_object_position_pts_get (so, pos);
		renderer  = g_object_new (GOG_TYPE_RENDERER,
					  "model", graph,
					  NULL);
		gog_renderer_update (renderer, pos[2] - pos[0], pos[3] - pos[1]);
		g_object_get (G_OBJECT (renderer), "view", &state->root_view, NULL);

		role = gog_object_find_role_by_name (chart, "Plot");
		if (role != NULL) {
			GSList *plots = gog_object_get_children
				(chart, gog_object_find_role_by_name (chart, "Plot"));
			if (plots != NULL && plots->data != NULL) {
				odf_write_plot (state, so, GOG_OBJECT (graph),
						chart, plots->data, plots->next);
				plot_written = TRUE;
			}
			g_slist_free (plots);
		}
		g_object_unref (state->root_view);
		state->root_view = NULL;
		g_object_unref (renderer);
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
odf_write_images (SheetObjectImage *soi, char const *name, GnmOOExport *state)
{
	char *image_type;
	char *fullname;
	GsfOutput  *child;
	GOImage *image;

	g_object_get (G_OBJECT (soi),
		      "image-type", &image_type,
		      "image", &image,
		      NULL);
	fullname = g_strdup_printf ("Pictures/%s.%s", name, image_type);

	child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
					    "compression-level", GSF_ZIP_DEFLATED,
					    NULL);
	if (NULL != child) {
		gsize length;
		guint8 const *data = go_image_get_data (image, &length);
		gsf_output_write (child, length, data);
		gsf_output_close (child);
		g_object_unref (child);
	}

	g_free (fullname);
	g_free (image_type);
	g_object_unref (image);

	odf_update_progress (state, state->graph_progress);
}

static void
odf_write_drop (GnmOOExport *state, G_GNUC_UNUSED GOStyle const *style,
		GogObject const *obj)
{
	GogObjectRole const *h_role = gog_object_find_role_by_name
		(obj->parent, "Horizontal drop lines");
	gboolean vertical = !(h_role == obj->role);

	odf_add_bool (state->xml, CHART "vertical", vertical);
}

static void
odf_write_reg_name (GnmOOExport *state, GogObject const *obj)
{
	if (state->with_extension)
		odf_add_expr (state, obj, -1, GNMSTYLE "regression-name",
				 LOEXT "regression-name");
}

static void
odf_write_plot_style_affine (GsfXMLOut *xml, GogObject const *plot, float intercept)
{
	gboolean b;
	if (gnm_object_has_readable_prop (plot, "affine", G_TYPE_BOOLEAN, &b)) {
		odf_add_bool (xml, GNMSTYLE "regression-affine", b);
		odf_add_bool (xml, LOEXT "regression-force-intercept", !b);
		go_xml_out_add_double (xml, LOEXT "regression-intercept-value", intercept);
	}
}


static void
odf_write_lin_reg (GnmOOExport *state, G_GNUC_UNUSED GOStyle const *style,
		   GogObject const *obj)
{
	gsf_xml_out_add_cstr (state->xml, CHART "regression-type",  "linear");
	if (state->with_extension) {
		odf_write_plot_style_uint (state->xml, obj,
					   "dims", GNMSTYLE "regression-polynomial-dims");
		odf_write_plot_style_uint (state->xml, obj,
					   "dims", LOEXT "regression-max-degree");
		odf_write_plot_style_affine (state->xml, obj, 0.);
	}
	odf_write_reg_name (state, obj);
}

static void
odf_write_polynom_reg (GnmOOExport *state, G_GNUC_UNUSED GOStyle const *style,
		       GogObject const *obj)
{
	if (state->with_extension) {
		gsf_xml_out_add_cstr (state->xml, CHART "regression-type",
				      GNMSTYLE "polynomial");
		odf_write_plot_style_uint (state->xml, obj,
					  "dims", GNMSTYLE "regression-polynomial-dims");
		odf_write_plot_style_uint (state->xml, obj,
					   "dims", LOEXT "regression-max-degree");
		odf_write_plot_style_affine (state->xml, obj, 0.);
	}
	odf_write_reg_name (state, obj);
}

static void
odf_write_exp_reg (GnmOOExport *state, G_GNUC_UNUSED GOStyle const *style,
		   G_GNUC_UNUSED GogObject const *obj)
{
	gsf_xml_out_add_cstr (state->xml, CHART "regression-type",  "exponential");
	if (state->with_extension)
		odf_write_plot_style_affine (state->xml, obj, 1.);
	odf_write_reg_name (state, obj);
}

static void
odf_write_power_reg (GnmOOExport *state, G_GNUC_UNUSED GOStyle const *style,
		     G_GNUC_UNUSED GogObject const *obj)
{
	gsf_xml_out_add_cstr (state->xml, CHART "regression-type",  "power");
	odf_write_reg_name (state, obj);
}

static void
odf_write_log_reg (GnmOOExport *state, G_GNUC_UNUSED GOStyle const *style,
		   G_GNUC_UNUSED GogObject const *obj)
{
	gsf_xml_out_add_cstr (state->xml, CHART "regression-type",  "logarithmic");
	odf_write_reg_name (state, obj);
}

static void
odf_write_log_fit_reg (GnmOOExport *state, G_GNUC_UNUSED GOStyle const *style,
		       G_GNUC_UNUSED GogObject const *obj)
{
	if (state->with_extension)
		gsf_xml_out_add_cstr (state->xml, CHART "regression-type",
				      GNMSTYLE "log-fit");
	odf_write_reg_name (state, obj);
}

static void
odf_write_movig_avg_reg (GnmOOExport *state, G_GNUC_UNUSED GOStyle const *style,
			 G_GNUC_UNUSED GogObject const *obj)
{
	if (state->with_extension)
		gsf_xml_out_add_cstr (state->xml, CHART "regression-type",
				      GNMSTYLE "moving-average");
	odf_write_reg_name (state, obj);
}

static void
odf_write_exp_smooth_reg (GnmOOExport *state, G_GNUC_UNUSED GOStyle const *style,
			  G_GNUC_UNUSED GogObject const *obj)
{
	if (state->with_extension)
		gsf_xml_out_add_cstr (state->xml, CHART "regression-type",
				      GNMSTYLE "exponential-smoothed");
	odf_write_reg_name (state, obj);
}

static void
odf_write_pie_point (GnmOOExport *state, G_GNUC_UNUSED GOStyle const *style,
		     GogObject const *obj)
{
	double separation = 0.;

	if (gnm_object_has_readable_prop (obj, "separation",
					  G_TYPE_DOUBLE, &separation)) {
		gsf_xml_out_add_int (state->xml,
				     CHART "pie-offset",
				     round (separation * 100));
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
		{"GogAxisLine", odf_write_axisline_style},
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
		{"GogLineSeries", odf_write_interpolation_attribute},
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
		g_object_unref (child);
	} else
		g_free (manifest_name);



}

static void
odf_write_graphs (SheetObject *so, char const *name, GnmOOExport *state)
{
	GogGraph *graph = sheet_object_graph_get_gog (so);
	GogObjectRole const *role = gog_object_find_role_by_name (GOG_OBJECT (graph), "Chart");
	GSList *l, *chart_list = gog_object_get_children (GOG_OBJECT (graph), role);
	gint n = 0;
	guint num = g_slist_length (chart_list);
	gchar *chartname;
	float progress = state->graph_progress / num;

	l = chart_list;

	while (NULL != chart_list) {
		GsfOutput  *child;
		GogObject const	*chart = chart_list->data;
		chartname = g_strdup_printf ("%s-%i", name, n);
		g_hash_table_remove_all (state->xl_styles);

		state->object_name = chartname;

		child = gsf_outfile_new_child_full
			(state->outfile, chartname, TRUE,
			 "compression-level", GSF_ZIP_DEFLATED,
			 NULL);
		if (NULL != child) {
			char *fullname = g_strdup_printf ("%s/content.xml", chartname);
			GsfOutput  *sec_child;

			state->chart_props_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
									 NULL, NULL);
			odf_fill_chart_props_hash (state);

			sec_child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
								"compression-level", GSF_ZIP_DEFLATED,
								NULL);
			if (NULL != sec_child) {
				odf_write_graph_content (state, sec_child, so, chart);
				gsf_output_close (sec_child);
				g_object_unref (sec_child);
			}
			g_free (fullname);

			odf_update_progress (state, 4 * progress);

			fullname = g_strdup_printf ("%s/meta.xml", chartname);
			sec_child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
								"compression-level", GSF_ZIP_DEFLATED,
								NULL);
			if (NULL != sec_child) {
				odf_write_meta_graph (state, sec_child);
				gsf_output_close (sec_child);
				g_object_unref (sec_child);
			}
			g_free (fullname);
			odf_update_progress (state, progress / 2);

			fullname = g_strdup_printf ("%s/styles.xml", chartname);
			sec_child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
								"compression-level", GSF_ZIP_DEFLATED,
								NULL);
			if (NULL != sec_child) {
				odf_write_graph_styles (state, sec_child);
				gsf_output_close (sec_child);
				g_object_unref (sec_child);
			}
			g_free (fullname);

			gnm_hash_table_foreach_ordered
				(state->graph_fill_images,
				 (GHFunc) odf_write_fill_images,
				 by_value_str,
				 state);

			g_hash_table_remove_all (state->graph_dashes);
			g_hash_table_remove_all (state->graph_hatches);
			g_hash_table_remove_all (state->graph_gradients);
			g_hash_table_remove_all (state->graph_fill_images);

			g_hash_table_unref (state->chart_props_hash);
			state->chart_props_hash = NULL;
			odf_update_progress (state, progress * (3./2.));

			gsf_output_close (child);
			g_object_unref (child);

			fullname = g_strdup_printf ("Pictures/%s", chartname);
			sec_child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
								"compression-level", GSF_ZIP_DEFLATED,
								NULL);
			if (NULL != sec_child) {
				if (!gog_graph_export_image (graph, GO_IMAGE_FORMAT_SVG,
							     sec_child, 100., 100.))
					g_print ("Failed to create svg image of graph.\n");
				gsf_output_close (sec_child);
				g_object_unref (sec_child);
			}
			g_free (fullname);

			odf_update_progress (state, progress);

			fullname = g_strdup_printf ("Pictures/%s.png", chartname);
			sec_child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
								"compression-level", GSF_ZIP_DEFLATED,
								NULL);
			if (NULL != sec_child) {
				if (!gog_graph_export_image (graph, GO_IMAGE_FORMAT_PNG,
							     sec_child, 100., 100.))
					g_print ("Failed to create png image of graph.\n");
				gsf_output_close (sec_child);
				g_object_unref (sec_child);
			}
			g_free (fullname);
			odf_update_progress (state, progress);
		}

		chart_list = chart_list->next;
		n++;
		g_free (chartname);
	}
	state->object_name = NULL;
	g_slist_free (l);
}


/**********************************************************************************/

static void
openoffice_file_save_real (G_GNUC_UNUSED  GOFileSaver const *fs, GOIOContext *ioc,
			   WorkbookView const *wbv, GsfOutput *output,
			   gboolean with_extension)
{
	static struct {
		void (*func) (GnmOOExport *state, GsfOutput *child);
		char const *name;
		gboolean inhibit_compression;
	} const streams[] = {
		/* Must be first */
		{ odf_write_mimetype,	"mimetype",        TRUE  },

		{ odf_write_content,	"content.xml",     FALSE },
		{ odf_write_styles,	"styles.xml",      FALSE }, /* must follow content */
		{ odf_write_meta,	"meta.xml",        FALSE },
		{ odf_write_settings,	"settings.xml",    FALSE },
	};

	GnmOOExport state;
	GnmLocale *locale;
	GError *err;
	unsigned i, ui;
	Sheet *sheet;
	GsfOutput *pictures;
	GsfOutput *manifest;
	GnmStyle *style;

	locale  = gnm_push_C_locale ();

	state.outfile = gsf_outfile_zip_new (output, &err);

	state.with_extension = with_extension;
	state.odf_version = gsf_odf_get_version ();
	state.odf_version_string = g_strdup (gsf_odf_get_version_string ());
	state.ioc = ioc;
	state.wbv = wbv;
	state.wb  = wb_view_get_workbook (wbv);
	state.conv = odf_expr_conventions_new (&state);
	state.openformula_namemap = NULL;
	state.openformula_handlermap = NULL;
	state.graphs = g_hash_table_new_full (g_direct_hash, g_direct_equal,
					       NULL, (GDestroyNotify) g_free);
	state.images = g_hash_table_new_full (g_direct_hash, g_direct_equal,
					       NULL, (GDestroyNotify) g_free);
	state.controls = g_hash_table_new_full (g_direct_hash, g_direct_equal,
					       NULL, (GDestroyNotify) g_free);
	state.named_cell_styles = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						   NULL, (GDestroyNotify) g_free);
	state.named_cell_style_regions = g_hash_table_new_full (g_direct_hash, g_direct_equal,
								(GDestroyNotify) gnm_style_region_free,
								(GDestroyNotify) g_free);
	state.cell_styles = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						   NULL, (GDestroyNotify) g_free);
	state.so_styles = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						   NULL, (GDestroyNotify) g_free);
	state.xl_styles =  g_hash_table_new_full (g_str_hash, g_str_equal,
						  (GDestroyNotify) g_free, (GDestroyNotify) g_free);
	for (ui = 0; ui < G_N_ELEMENTS (state.style_names); ui++)
		state.style_names[ui] =
			g_hash_table_new_full (g_direct_hash, g_direct_equal,
					       NULL, (GDestroyNotify) g_free);
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
						     (GDestroyNotify) g_free,
						     (GDestroyNotify) g_free);
	state.text_colours = g_hash_table_new_full (g_str_hash, g_str_equal,
						    (GDestroyNotify) g_free,
						    (GDestroyNotify) g_free);
	state.font_sizes = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);
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
	if (NULL != (style = sheet_style_default (sheet))) {
		GnmRange r = {{0,0},{0,0}};
		/* We need to make sure any referenced styles are added to the named hash */
		state.default_style_region = gnm_style_region_new (&r, style);
		odf_store_this_named_style (state.default_style_region->style, "Gnumeric-default",
					    &state.default_style_region->range,
					    &state);
		gnm_style_unref (style);
	} else {
		GnmRange r = {{0,0},{0,0}};
		state.default_style_region = gnm_style_region_new (&r, NULL);
	}

	for (i = 0 ; i < G_N_ELEMENTS (streams); i++) {
		int comp_level = streams[i].inhibit_compression
			? GSF_ZIP_STORED
			: GSF_ZIP_DEFLATED;
		GsfOutput *child = gsf_outfile_new_child_full
			(state.outfile, streams[i].name, FALSE,
			 "compression-level", comp_level,
			 NULL);
		if (NULL != child) {
			streams[i].func (&state, child);
			gsf_output_close (child);
			g_object_unref (child);
		}
		odf_update_progress (&state, state.sheet_progress);
	}

	state.graph_progress = ((float) PROGRESS_STEPS) / 2 /
		(8 * g_hash_table_size (state.graphs) + g_hash_table_size (state.images) + 1);
	go_io_progress_message (state.ioc, _("Writing Sheet Objects..."));

        pictures = gsf_outfile_new_child_full (state.outfile, "Pictures", TRUE,
								"compression-level", GSF_ZIP_DEFLATED,
								NULL);
	gnm_hash_table_foreach_ordered
		(state.graphs,
		 (GHFunc) odf_write_graphs,
		 by_value_str,
		 &state);
	gnm_hash_table_foreach_ordered
		(state.images,
		 (GHFunc) odf_write_images,
		 by_value_str,
		 &state);
	if (NULL != pictures) {
		gsf_output_close (pictures);
		g_object_unref (pictures);
	}

	/* Need to write the manifest */
	manifest = gsf_outfile_new_child_full
		(state.outfile, "META-INF/manifest.xml", FALSE,
		 "compression-level", GSF_ZIP_DEFLATED,
		 NULL);
	if (manifest) {
		odf_write_manifest (&state, manifest);
		gsf_output_close (manifest);
		g_object_unref (manifest);
	} else {
		/* Complain fiercely? */
	}

	g_free (state.conv);
	if (state.openformula_namemap)
		g_hash_table_destroy (state.openformula_namemap);
	if (state.openformula_handlermap)
		g_hash_table_destroy (state.openformula_handlermap);

	go_io_value_progress_update (state.ioc, PROGRESS_STEPS);
	go_io_progress_unset (state.ioc);
	gsf_output_close (GSF_OUTPUT (state.outfile));
	g_object_unref (state.outfile);

	g_free (state.odf_version_string);

	gnm_pop_C_locale (locale);
	g_hash_table_unref (state.graphs);
	g_hash_table_unref (state.images);
	g_hash_table_unref (state.controls);
	g_hash_table_unref (state.named_cell_styles);
	g_hash_table_unref (state.named_cell_style_regions);
	g_hash_table_unref (state.cell_styles);
	g_hash_table_unref (state.so_styles);
	g_hash_table_unref (state.xl_styles);
	for (ui = 0; ui < G_N_ELEMENTS (state.style_names); ui++)
		g_hash_table_unref (state.style_names[ui]);
	g_hash_table_unref (state.graph_dashes);
	g_hash_table_unref (state.graph_hatches);
	g_hash_table_unref (state.graph_gradients);
	g_hash_table_unref (state.graph_fill_images);
	g_hash_table_unref (state.arrow_markers);
	g_hash_table_unref (state.text_colours);
	g_hash_table_unref (state.font_sizes);
	g_slist_free_full (state.col_styles,  col_row_styles_free);
	g_slist_free_full (state.row_styles,  col_row_styles_free);
	if (state.default_style_region)
		gnm_style_region_free (state.default_style_region);
	go_format_unref (state.time_fmt);
	go_format_unref (state.date_fmt);
	go_format_unref (state.date_long_fmt);

	ods_render_ops_clear (odf_render_ops);
	ods_render_ops_clear (odf_render_ops_to_xl);
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
