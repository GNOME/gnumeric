/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * openoffice-write.c : export OpenOffice OASIS .ods files
 *
 * Copyright (C) 2004-2006 Jody Goldberg (jody@gnome.org)
 *
 * Copyright (C) 2006-2009 Andreas J. Guelzow (aguelzow@pyrshep.ca)
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

/* change the following to 12 to switch to ODF 1.2 creation. Note that this is not tested and */
/* changes in GOFFICE are also required. */

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
	GHashTable *xl_styles;
	GHashTable *xl_styles_neg;
	GHashTable *xl_styles_zero;
	GHashTable *xl_styles_conditional;
	GnmStyle *default_style;
	ColRowInfo const *row_default;
	ColRowInfo const *column_default;
	GHashTable *objects;
	gboolean with_extension;
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

static void
gnm_xml_out_add_hex_color (GsfXMLOut *o, char const *id, GnmColor const *c, int pattern)
{
	g_return_if_fail (c != NULL);

	if (pattern == 0)
		gsf_xml_out_add_cstr_unchecked (o, id, "transparent");
	else {
		char *color;
		color = g_strdup_printf ("#%.2x%.2x%.2x",
					 GO_COLOR_UINT_R (c->go_color),
					 GO_COLOR_UINT_G (c->go_color),
					 GO_COLOR_UINT_B (c->go_color));
		gsf_xml_out_add_cstr_unchecked (o, id, color);
		g_free (color);
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
	gsf_xml_out_end_element (state->xml); /* </style:table-properties> */

	gsf_xml_out_end_element (state->xml); /* </style:style> */
}

static char *
table_style_name (Sheet const *sheet)
{
	return g_strdup_printf ("ta-%c-%s",
		(sheet->visibility == GNM_SHEET_VISIBILITY_VISIBLE) ? 'v' : 'h',
		sheet->text_is_rtl ? "rl" : "lr");
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
	GHashTable *known = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free, NULL);

	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet const *sheet = workbook_sheet_by_index (state->wb, i);
		char *name = table_style_name (sheet);
		if (NULL == g_hash_table_lookup (known, name)) {
			g_hash_table_replace (known, name, name);
			odf_write_table_style (state, sheet, name);
		} else
			g_free (name);
	}
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
		char const *source = "fix";
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
			source = "value-type";
			gnum_specs = TRUE;
			break;
		}
		if (align != HALIGN_GENERAL)
			gsf_xml_out_add_cstr (state->xml, FOSTYLE "text-align", alignment);
		gsf_xml_out_add_cstr (state->xml, STYLE "text-align-source", source);
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
		gsf_xml_out_add_int (state->xml, FOSTYLE "font-size",
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
odf_write_style_goformat_name (GnmOOExport *state, GOFormat *gof)
{
	char const *name;

	if ((gof == NULL) || go_format_is_markup (gof)
	    || go_format_is_text (gof))
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
		GOFormat *format = gnm_style_get_format(style);
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
odf_write_frame (GnmOOExport *state, SheetObject *so)
{
	SheetObjectAnchor const *anchor = sheet_object_get_anchor (so);
	double res_pts[4] = {0.,0.,0.,0.};
	GnmCellRef ref;
	GnmRange const *r = &anchor->cell_bound;
	GnmExprTop const *texpr;
	GnmParsePos pp;
	char *formula;

	sheet_object_anchor_to_offset_pts (anchor, state->sheet, res_pts);

	gsf_xml_out_start_element (state->xml, DRAW "frame");
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
	gsf_xml_out_add_cstr (state->xml, TABLE "end-cell-address", odf_strip_brackets (formula));
	g_free (formula);

	if (IS_SHEET_OBJECT_GRAPH (so)) {
		char const *name = g_hash_table_lookup (state->objects, so);
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
			full_name = g_strdup_printf ("./Pictures/%s", name);
			gsf_xml_out_start_element (state->xml, DRAW "image");
			gsf_xml_out_add_cstr (state->xml, XLINK "href", full_name);
			g_free (full_name);
			gsf_xml_out_add_cstr (state->xml, XLINK "type", "simple");
			gsf_xml_out_add_cstr (state->xml, XLINK "show", "embed");
			gsf_xml_out_add_cstr (state->xml, XLINK "actuate", "onLoad");
			gsf_xml_out_end_element (state->xml); /*  DRAW "image" */
			full_name = g_strdup_printf ("./Pictures/%s.png", name);
			gsf_xml_out_start_element (state->xml, DRAW "image");
			gsf_xml_out_add_cstr (state->xml, XLINK "href", full_name);
			g_free (full_name);
			gsf_xml_out_add_cstr (state->xml, XLINK "type", "simple");
			gsf_xml_out_add_cstr (state->xml, XLINK "show", "embed");
			gsf_xml_out_add_cstr (state->xml, XLINK "actuate", "onLoad");
			gsf_xml_out_end_element (state->xml); /*  DRAW "image" */
		} else
			g_warning ("Graph is missing from hash.");
	} else {
		gsf_xml_out_start_element (state->xml, DRAW "text-box");
		gsf_xml_out_simple_element (state->xml, TEXT "p", "Missing Sheet Object");
		gsf_xml_out_end_element (state->xml); /*  DRAW "text-box" */
	}

	gsf_xml_out_end_element (state->xml); /*  DRAW "frame" */
}

static void
odf_write_objects (GnmOOExport *state, GSList *objects)
{
	GSList *l;

	for (l = objects; l != NULL; l = l->next) {
		SheetObject *so = l->data;
		if (so == NULL) {
			g_warning ("NULL sheet object encountered.");
			continue;
		}
		if (IS_CELL_COMMENT (so))
			odf_write_comment (state, CELL_COMMENT (so));
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
			if (name != NULL)
				gsf_xml_out_add_cstr (state->xml,
						      TABLE "style-name", name);
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
			if (name != NULL)
				gsf_xml_out_add_cstr (state->xml,
						      TABLE "style-name", name);
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
			GString *str = g_string_new (NULL);

			gsf_xml_out_add_cstr_unchecked (state->xml,
							OFFICE "value-type", "float");
			value_get_as_gstring (cell->value, str, state->conv);
			gsf_xml_out_add_cstr (state->xml, OFFICE "value", str->str);

			g_string_free (str, TRUE);
		}
			break;
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
	int i;
	GSList *sheet_merges = NULL;
	GnmPageBreaks *pb = sheet->print_info->page_breaks.v;
	extent = sheet_get_extent (sheet, FALSE);

	/* include collapsed or hidden cols and rows */
	for (i = max_rows ; i-- > extent.end.row ; )
		if (!colrow_is_empty (sheet_row_get (sheet, i))) {
			extent.end.row = i;
			break;
		}
	for (i = max_cols ; i-- > extent.end.col ; )
		if (!colrow_is_empty (sheet_col_get (sheet, i))) {
			extent.end.col = i;
			break;
		}

	style_extent = extent;
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
}


static void
odf_write_content (GnmOOExport *state, GsfOutput *child)
{
	int i;
	int graph_n = 1;
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
	gsf_xml_out_end_element (state->xml); /* </office:automatic-styles> */

	gsf_xml_out_start_element (state->xml, OFFICE "body");
	gsf_xml_out_start_element (state->xml, OFFICE "spreadsheet");

	odf_print_spreadsheet_content_prelude (state);

	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet *sheet = workbook_sheet_by_index (state->wb, i);
		char *style_name;
		GnmRange    *p_area;
		GSList *l, *graphs;

		state->sheet = sheet;

		graphs = sheet_objects_get (sheet, NULL, SHEET_OBJECT_GRAPH_TYPE);
		for (l = graphs; l != NULL; l = l->next)
			g_hash_table_insert (state->objects, l->data,
					     g_strdup_printf ("Graph%i", graph_n++));
		g_slist_free (graphs);

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

		odf_write_sheet (state);
		gsf_xml_out_end_element (state->xml); /* </table:table> */

		has_autofilters |= (sheet->filters != NULL);
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
odf_write_settings (GnmOOExport *state, GsfOutput *child)
{
	int i;

	state->xml = gsf_xml_out_new (child);
	gsf_xml_out_start_element (state->xml, OFFICE "document-settings");
	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version",
					get_gsf_odf_version_string ());
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
	fullname = g_strdup_printf ("Pictures/%s", name);
	odf_file_entry (state->xml, "image/svg+xml", fullname);
	g_free(fullname);
	fullname = g_strdup_printf ("Pictures/%s.png", name);
	odf_file_entry (state->xml, "image/png", fullname);
	g_free(fullname);
}

static void
odf_write_manifest (GnmOOExport *state, GsfOutput *child)
{
	GsfXMLOut *xml = gsf_xml_out_new (child);
	gsf_xml_out_set_doc_type (xml, "\n");
	gsf_xml_out_start_element (xml, MANIFEST "manifest");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:manifest",
		"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0");
	odf_file_entry (xml, "application/vnd.oasis.opendocument.spreadsheet" ,"/");
	odf_file_entry (xml, "", "Pictures/");
	odf_file_entry (xml, "text/xml", "content.xml");
	odf_file_entry (xml, "text/xml", "styles.xml");
	odf_file_entry (xml, "text/xml", "meta.xml");
	odf_file_entry (xml, "text/xml", "settings.xml");

	state->xml = xml;
	g_hash_table_foreach (state->objects, (GHFunc) odf_write_graph_manifest, state);

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
	ODF_POLAR
} odf_chart_type_t;

static void
odf_write_standard_series (GnmOOExport *state, GSList const *series)
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
				str = g_strdup_printf ("series%i", i);
				gsf_xml_out_add_cstr (state->xml, CHART "style-name", str);
				g_free (str);
				dat = gog_series_get_name (GOG_SERIES (series->data));
				if (NULL != dat) {
					texpr = gnm_go_data_get_expr (dat);
					if (NULL != texpr) {
						str = gnm_expr_top_as_string (texpr, &pp, state->conv);
						gsf_xml_out_add_cstr (state->xml, CHART "label-cell-address",
								      odf_strip_brackets (str));
						g_free (str);
					}
				}
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
				str = g_strdup_printf ("series%i", i);
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
				str = g_strdup_printf ("series%i", i);
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
				str = g_strdup_printf ("series%i", i);
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
					str = g_strdup_printf ("series%i", i);
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
odf_write_bar_col_plot_style (GnmOOExport *state, G_GNUC_UNUSED GogObject const *chart, GogObject const *plot)
{
	gboolean horizontal = FALSE;

	g_object_get (G_OBJECT (plot), "horizontal", &horizontal, NULL);
	/* Note: horizontal refers to the bars and vertical to the x-axis */
	odf_add_bool (state->xml, CHART "vertical", horizontal);
}

static void
odf_write_ring_plot_style (GnmOOExport *state, G_GNUC_UNUSED GogObject const *chart, G_GNUC_UNUSED GogObject const *plot)
{
	odf_add_percent (state->xml, CHART "hole-size", 0.5);
}

static void
odf_write_line_chart_style (GnmOOExport *state, G_GNUC_UNUSED GogObject const *chart, G_GNUC_UNUSED GogObject const *plot)
{
	gsf_xml_out_add_cstr (state->xml, CHART "symbol-type", "none");
}

static void
odf_write_scatter_chart_style (GnmOOExport *state, G_GNUC_UNUSED GogObject const *chart, G_GNUC_UNUSED GogObject const *plot)
{
	gsf_xml_out_add_cstr (state->xml, DRAW "stroke", "none");
	odf_add_bool (state->xml, CHART "lines", FALSE);
}

static void
odf_write_surface_chart_style (GnmOOExport *state, G_GNUC_UNUSED GogObject const *chart, G_GNUC_UNUSED GogObject const *plot)
{
	odf_add_bool (state->xml, CHART "three-dimensional", TRUE);
}

static void
odf_write_xl_surface_chart_style (GnmOOExport *state, G_GNUC_UNUSED GogObject const *chart, G_GNUC_UNUSED GogObject const *plot)
{
	odf_add_bool (state->xml, CHART "three-dimensional", TRUE);
	if (state->with_extension)
		odf_add_bool (state->xml, GNMSTYLE "multi-series", TRUE);
}

static void
odf_write_contour_chart_style (GnmOOExport *state, G_GNUC_UNUSED GogObject const *chart, G_GNUC_UNUSED GogObject const *plot)
{
	odf_add_bool (state->xml, CHART "three-dimensional", FALSE);
}

static void
odf_write_scatter_series_style (GnmOOExport *state, G_GNUC_UNUSED GogObject const *series)
{
	gsf_xml_out_add_cstr (state->xml, DRAW "stroke", "none");
	odf_add_bool (state->xml, CHART "lines", FALSE);
	gsf_xml_out_add_cstr (state->xml, CHART "symbol-type", "automatic");
}

static void
odf_write_axis_style (GnmOOExport *state, GogObject const *chart,
		      char const *style_label, GogObject const *axis, gboolean reverse)
{
	odf_start_style (state->xml, style_label, "chart");
	gsf_xml_out_start_element (state->xml, STYLE "chart-properties");

	gsf_xml_out_add_cstr (state->xml, CHART "axis-position", "start");
	odf_add_bool (state->xml, CHART "display-label", TRUE);

	if (axis != NULL) {
		char const *type = NULL;
		double minima = 0., maxima = 0.;

		g_object_get (G_OBJECT (axis), "map-name", &type, NULL);
		odf_add_bool (state->xml, CHART "logarithmic", 0 != strcmp (type, "Linear"));
		if (gog_axis_get_bounds (GOG_AXIS (axis), &minima, &maxima)) {
			gsf_xml_out_add_float (state->xml, CHART "minimum", minima, -1);
			gsf_xml_out_add_float (state->xml, CHART "maximum", maxima, -1);
		}
	}

	if (get_gsf_odf_version () > 101)
		odf_add_bool (state->xml, CHART "reverse-direction", reverse);
	gsf_xml_out_end_element (state->xml); /* </style:chart-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */
}

static void
odf_write_circle_axes_styles (GnmOOExport *state, GogObject const *chart,
			     G_GNUC_UNUSED GogObject const *plot)
{
	odf_write_axis_style (state, chart, "yaxis", gog_object_get_child_by_name (chart, "Y-Axis"), TRUE);
	odf_write_axis_style (state, chart, "xaxis", gog_object_get_child_by_name (chart, "X-Axis"), TRUE);
}

static void
odf_write_radar_axes_styles (GnmOOExport *state, GogObject const *chart,
			     G_GNUC_UNUSED GogObject const *plot)
{
	odf_write_axis_style (state, chart, "yaxis", gog_object_get_child_by_name (chart, "Radial-Axis"), FALSE);
	odf_write_axis_style (state, chart, "xaxis", gog_object_get_child_by_name (chart, "Circular-Axis"), FALSE);
}

static void
odf_write_dropbar_axes_styles (GnmOOExport *state, GogObject const *chart,
				 GogObject const *plot)
{
	GObjectClass *klass = G_OBJECT_GET_CLASS (G_OBJECT (plot));
	gboolean horizontal = FALSE;
	if (NULL != g_object_class_find_property (klass,  "horizontal"))
		g_object_get (G_OBJECT (plot), "horizontal", &horizontal, NULL);
	odf_write_axis_style (state, chart, horizontal ? "xaxis" : "yaxis",
			      gog_object_get_child_by_name (chart, "Y-Axis"), horizontal);
	odf_write_axis_style (state, chart, horizontal ? "yaxis" : "xaxis",
			      gog_object_get_child_by_name (chart, "X-Axis"), FALSE);
}

static void
odf_write_standard_axes_styles (GnmOOExport *state, GogObject const *chart,
				GogObject const *plot)
{
	GObjectClass *klass = G_OBJECT_GET_CLASS (G_OBJECT (plot));
	gboolean horizontal = FALSE;
	if (NULL != g_object_class_find_property (klass,  "horizontal"))
		g_object_get (G_OBJECT (plot), "horizontal", &horizontal, NULL);
	odf_write_axis_style (state, chart, horizontal ? "xaxis" : "yaxis",
			      gog_object_get_child_by_name (chart, "Y-Axis"), FALSE);
	odf_write_axis_style (state, chart, horizontal ? "yaxis" : "xaxis",
			      gog_object_get_child_by_name (chart, "X-Axis"), FALSE);
}

static void
odf_write_surface_axes_styles (GnmOOExport *state, GogObject const *chart,
			       GogObject const *plot)
{
	odf_write_axis_style (state, chart, "zaxis", gog_object_get_child_by_name (chart, "Z-Axis"), FALSE);
	odf_write_standard_axes_styles (state, chart, plot);
}


static void
odf_write_axis (GnmOOExport *state, GogObject const *chart, char const *axis_role, char const *style_label,
	char const *dimension, odf_chart_type_t gtype)
{
	GogObject const *axis;

	if (axis_role == NULL)
		return;

	axis = gog_object_get_child_by_name (chart, axis_role);
	if (axis != NULL || (gtype == ODF_CIRCLE && *dimension == 'y') || (gtype == ODF_RING)) {
		gsf_xml_out_start_element (state->xml, CHART "axis");
		gsf_xml_out_add_cstr (state->xml, CHART "dimension", dimension);
		gsf_xml_out_add_cstr (state->xml, CHART "style-name", style_label);
		gsf_xml_out_end_element (state->xml); /* </chart:axis> */
	}
}

static void
odf_write_plot (GnmOOExport *state, SheetObject *so, GogObject const *chart, GogObject const *plot)
{
	char const *plot_type = G_OBJECT_TYPE_NAME (plot);
	SheetObjectAnchor const *anchor = sheet_object_get_anchor (so);
	double res_pts[4] = {0.,0.,0.,0.};
	GSList const *series, *l;
	int i;
	GogObject *wall = gog_object_get_child_by_name (plot, "Backplane");

	static struct {
		char const * type;
		char const *odf_plot_type;
		odf_chart_type_t gtype;
		double pad;
		char const * x_axis_name;
		char const * y_axis_name;
		char const * z_axis_name;
		void (*odf_write_axes_styles) (GnmOOExport *state, GogObject const *chart,
					       GogObject const *plot);
		void (*odf_write_chart_styles) (GnmOOExport *state, GogObject const *chart,
						GogObject const *plot);
		void (*odf_write_plot_styles) (GnmOOExport *state, GogObject const *chart,
					       GogObject const *plot);
		void (*odf_write_series) (GnmOOExport *state, GSList const *series);
		void (*odf_write_series_style) (GnmOOExport *state, GogObject const *series);
	} *this_plot, plots[] = {
		{ "GogBarColPlot", "chart:bar", ODF_BARCOL,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  NULL, odf_write_bar_col_plot_style, odf_write_standard_series, NULL},
		{ "GogLinePlot", "chart:line", ODF_LINE,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_line_chart_style, NULL, odf_write_standard_series, NULL},
		{ "GogPolarPlot", "gnm:polar", ODF_POLAR,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  NULL, NULL, odf_write_standard_series, NULL},
		{ "GogAreaPlot", "chart:area", ODF_AREA,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  NULL, NULL, odf_write_standard_series, NULL},
		{ "GogDropBarPlot", "chart:gantt", ODF_DROPBAR,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_dropbar_axes_styles,
		  NULL, odf_write_bar_col_plot_style, odf_write_gantt_series, NULL},
		{ "GogMinMaxPlot", "chart:stock", ODF_MINMAX,
		  10., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  NULL, NULL, odf_write_min_max_series, NULL},
		{ "GogPiePlot", "chart:circle", ODF_CIRCLE,
		  5., "X-Axis", "Y-Axis", NULL, odf_write_circle_axes_styles,
		  NULL, NULL, odf_write_standard_series, NULL},
		{ "GogRadarPlot", "chart:radar", ODF_RADAR,
		  10., "Circular-Axis", "Radial-Axis", NULL, odf_write_radar_axes_styles,
		  NULL, NULL, odf_write_standard_series, NULL},
		{ "GogRadarAreaPlot", "chart:filled-radar", ODF_RADARAREA,
		  10., "X-Axis", "Y-Axis", NULL, odf_write_radar_axes_styles,
		  NULL, NULL, odf_write_standard_series, NULL},
		{ "GogRingPlot", "chart:ring", ODF_RING,
		  10., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  NULL, odf_write_ring_plot_style, odf_write_standard_series, NULL},
		{ "GogXYPlot", "chart:scatter", ODF_SCATTER,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_scatter_chart_style, NULL, odf_write_standard_series, odf_write_scatter_series_style},
		{ "GogContourPlot", "chart:surface", ODF_SURF,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_contour_chart_style, NULL, odf_write_bubble_series, NULL},
		{ "GogXYZContourPlot", "gnm:xyz-surface", ODF_XYZ_SURF,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  odf_write_contour_chart_style, NULL, odf_write_bubble_series, NULL},
		{ "GogXYZSurfacePlot", "gnm:xyz-surface", ODF_XYZ_GNM_SURF,
		  20., "X-Axis", "Y-Axis", "Z-Axis", odf_write_surface_axes_styles,
		  odf_write_surface_chart_style, NULL, odf_write_bubble_series, NULL},
		{ "GogSurfacePlot", "chart:surface", ODF_GNM_SURF,
		  20., "X-Axis", "Y-Axis", "Z-Axis", odf_write_surface_axes_styles,
		  odf_write_surface_chart_style, NULL, odf_write_bubble_series, NULL},
		{ "GogBubblePlot", "chart:bubble", ODF_BUBBLE,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  NULL, NULL, odf_write_bubble_series, NULL},
		{ "GogXYColorPlot", "gnm:scatter-color", ODF_SCATTER_COLOUR,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  NULL, NULL, odf_write_bubble_series, NULL},
		{ "XLSurfacePlot", "chart:surface", ODF_GNM_SURF,
		  20., "X-Axis", "Y-Axis", "Z-Axis", odf_write_surface_axes_styles,
		  odf_write_xl_surface_chart_style, NULL, odf_write_standard_series, NULL},
		{ NULL, NULL, 0,
		  20., "X-Axis", "Y-Axis", NULL, odf_write_standard_axes_styles,
		  NULL, NULL, odf_write_standard_series, NULL}
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
		this_plot->odf_write_axes_styles (state, chart, plot);

	odf_start_style (state->xml, "plotstyle", "chart");
	gsf_xml_out_start_element (state->xml, STYLE "chart-properties");
	odf_add_bool (state->xml, CHART "auto-size", TRUE);

	if (this_plot->odf_write_chart_styles != NULL)
		this_plot->odf_write_chart_styles (state, chart, plot);

	gsf_xml_out_end_element (state->xml); /* </style:chart-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	odf_start_style (state->xml, "plotarea", "chart");
	gsf_xml_out_start_element (state->xml, STYLE "chart-properties");
	odf_add_bool (state->xml, CHART "auto-size", TRUE);

	if (this_plot->odf_write_plot_styles != NULL)
		this_plot->odf_write_plot_styles (state, chart, plot);

	gsf_xml_out_end_element (state->xml); /* </style:chart-properties> */
	gsf_xml_out_end_element (state->xml); /* </style:style> */

	for (l = series, i = 1; l != NULL; l = l->next) {
		char *name = g_strdup_printf ("series%i", i++);
		odf_start_style (state->xml, name, "chart");
		gsf_xml_out_start_element (state->xml, STYLE "chart-properties");
		odf_add_bool (state->xml, CHART "auto-size", TRUE);
		if (this_plot->odf_write_series_style != NULL)
			this_plot->odf_write_series_style (state, l->data);
		gsf_xml_out_end_element (state->xml); /* </style:chart-properties> */
		gsf_xml_out_end_element (state->xml); /* </style:style> */
		g_free (name);
	}

	if (wall != NULL) {
		odf_start_style (state->xml, "wallstyle", "chart");
		gsf_xml_out_start_element (state->xml, STYLE "graphic-properties");
		gsf_xml_out_add_cstr (state->xml, DRAW "fill", "solid");
/* 	gnm_xml_out_add_hex_color (state->xml, DRAW "fill-color", GnmColor const *c, 1) */
		gsf_xml_out_add_cstr (state->xml, DRAW "fill-color", "#D0D0D0");
		gsf_xml_out_end_element (state->xml); /* </style:graphic-properties> */
		gsf_xml_out_end_element (state->xml); /* </style:style> */
	}

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
	gsf_xml_out_start_element (state->xml, CHART "plot-area");
	gsf_xml_out_add_cstr (state->xml, CHART "style-name", "plotarea");
	if (get_gsf_odf_version () <= 101) {
		for ( ; NULL != series ; series = series->next) {
			GOData const *dat = gog_dataset_get_dim
				(GOG_DATASET (series->data), GOG_MS_DIM_VALUES);
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

	odf_write_axis (state, chart, this_plot->z_axis_name, "zaxis", "z", this_plot->gtype);
	odf_write_axis (state, chart, this_plot->y_axis_name, "yaxis", "y", this_plot->gtype);
	odf_write_axis (state, chart, this_plot->x_axis_name, "zaxis", "x", this_plot->gtype);

	if (this_plot->odf_write_series != NULL)
		this_plot->odf_write_series (state, series);

	if (wall != NULL) {
		gsf_xml_out_start_element (state->xml, CHART "wall");
		odf_add_pt (state->xml, SVG "width", res_pts[2] - res_pts[0] - 2 * this_plot->pad);
		gsf_xml_out_add_cstr (state->xml, CHART "style-name", "wallstyle");
		gsf_xml_out_end_element (state->xml); /* </chart:wall> */
	}
	gsf_xml_out_end_element (state->xml); /* </chart:plot_area> */
	gsf_xml_out_end_element (state->xml); /* </chart:chart> */
	gsf_xml_out_end_element (state->xml); /* </office:chart> */
	gsf_xml_out_end_element (state->xml); /* </office:body> */
}


static void
odf_write_graph_content (GnmOOExport *state, GsfOutput *child, SheetObject *so)
{
	int i;
	GogGraph const	*graph;

	state->xml = gsf_xml_out_new (child);
	gsf_xml_out_set_doc_type (state->xml, "\n");
	gsf_xml_out_start_element (state->xml, OFFICE "document-content");

	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version",
					get_gsf_odf_version_string ());

	graph = sheet_object_graph_get_gog (so);
	if (graph != NULL) {
		GogObject const	*chart = gog_object_get_child_by_name (GOG_OBJECT (graph), "Chart");
		if (chart != NULL) {
			GogObject const *plot = gog_object_get_child_by_name (GOG_OBJECT (chart), "Plot");
			if (plot != NULL)
				odf_write_plot (state, so, chart, plot);
		}
	}
	gsf_xml_out_end_element (state->xml); /* </office:document-content> */
	g_object_unref (state->xml);
	state->xml = NULL;
}

/**********************************************************************************/

static void
odf_write_graphs (SheetObject *graph, char const *name, GnmOOExport *state)
{
	GsfOutput  *child;

	child = gsf_outfile_new_child_full (state->outfile, name, TRUE,
				"compression-level", GSF_ZIP_DEFLATED,
					    NULL);
	if (NULL != child) {
		char *fullname = g_strdup_printf ("%s/content.xml", name);
		GsfOutput  *sec_child = gsf_outfile_new_child_full (state->outfile, fullname, FALSE,
							"compression-level", GSF_ZIP_DEFLATED,
							NULL);
		if (NULL != sec_child) {
			odf_write_graph_content (state, sec_child, graph);
			gsf_output_close (sec_child);
			g_object_unref (G_OBJECT (sec_child));
		}
		g_free (fullname);

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
	}
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
		{ odf_write_manifest,	"META-INF/manifest.xml" }
	};

	GnmOOExport state;
	GnmLocale  *locale;
	GError *err;
	unsigned i;
	Sheet *sheet;
	GsfOutput  *pictures;

	locale  = gnm_push_C_locale ();

	state.outfile = gsf_outfile_zip_new (output, &err);

	state.with_extension = with_extension;
	state.ioc = ioc;
	state.wbv = wbv;
	state.wb  = wb_view_get_workbook (wbv);
	state.conv = odf_expr_conventions_new ();
	state.objects = g_hash_table_new_full (g_direct_hash, g_direct_equal,
					       NULL, (GDestroyNotify) g_free);
	state.named_cell_styles = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						   NULL, (GDestroyNotify) g_free);
	state.cell_styles = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						   NULL, (GDestroyNotify) g_free);
	state.xl_styles =  g_hash_table_new_full (g_str_hash, g_str_equal,
						  (GDestroyNotify) g_free, (GDestroyNotify) g_free);
	state.xl_styles_neg =  g_hash_table_new_full (g_str_hash, g_str_equal,
						  (GDestroyNotify) g_free, (GDestroyNotify) g_free);
	state.xl_styles_zero =  g_hash_table_new_full (g_str_hash, g_str_equal,
						  (GDestroyNotify) g_free, (GDestroyNotify) g_free);
	state.xl_styles_conditional =  g_hash_table_new_full (g_str_hash, g_str_equal,
						  (GDestroyNotify) g_free, (GDestroyNotify) g_free);
	state.col_styles = NULL;
	state.row_styles = NULL;

	/* ODF dos not have defaults per table, so we use our first table for defaults only.*/
	sheet = workbook_sheet_by_index (state.wb, 0);

	state.column_default = &sheet->cols.default_style;
	state.row_default = &sheet->rows.default_style;
	if (NULL != (state.default_style = sheet_style_default (sheet)))
		/* We need to make sure any referenced styles are added to the named hash */
		odf_store_this_named_style (state.default_style, "Gnumeric-default", &state);

	for (i = 0 ; i < G_N_ELEMENTS (streams); i++) {
		GsfOutput  *child;
		child = gsf_outfile_new_child_full (state.outfile, streams[i].name, FALSE,
				/* do not compress the mimetype */
				"compression-level", ((0 == i) ? GSF_ZIP_STORED : GSF_ZIP_DEFLATED),
				NULL);
		if (NULL != child) {
			streams[i].func (&state, child);
			gsf_output_close (child);
			g_object_unref (G_OBJECT (child));
		}
	}

        pictures = gsf_outfile_new_child_full (state.outfile, "Pictures", TRUE,
								"compression-level", GSF_ZIP_DEFLATED,
								NULL);
	g_hash_table_foreach (state.objects, (GHFunc) odf_write_graphs, &state);
	if (NULL != pictures) {
		gsf_output_close (pictures);
		g_object_unref (G_OBJECT (pictures));
	}


	g_free (state.conv);

	gsf_output_close (GSF_OUTPUT (state.outfile));
	g_object_unref (G_OBJECT (state.outfile));

	gnm_pop_C_locale (locale);
	g_hash_table_unref (state.objects);
	g_hash_table_unref (state.named_cell_styles);
	g_hash_table_unref (state.cell_styles);
	g_hash_table_unref (state.xl_styles);
	g_hash_table_unref (state.xl_styles_neg);
	g_hash_table_unref (state.xl_styles_zero);
	g_hash_table_unref (state.xl_styles_conditional);
	g_slist_free (state.col_styles);
	g_slist_free (state.row_styles);
	gnm_style_unref (state.default_style);
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
