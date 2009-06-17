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
#include <solver.h>
#include <sheet-filter.h>
#include <sheet-object-cell-comment.h>
#include <print-info.h>
#include <parse-util.h>
#include <tools/scenarios.h>
#include <gutils.h>
#include <xml-io.h>


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
#define GNMSTYLE "gnm:"  /* We use this for attributes and elements not supported by ODF */

typedef struct {
	GsfXMLOut *xml;
	IOContext *ioc;
	WorkbookView const *wbv;
	Workbook const	   *wb;
	GnmConventions *conv;
	GSList *row_styles;
	GSList *col_styles;
	GHashTable *cell_styles;
	GHashTable *xl_styles;
	GHashTable *xl_styles_neg;
	GHashTable *xl_styles_zero;
	GHashTable *xl_styles_conditional;
	GnmStyle *default_style;
	ColRowInfo const *row_default;
	ColRowInfo const *column_default;
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
odf_add_pt (GsfXMLOut *xml, char const *id, float l)
{
	GString *str = g_string_new (NULL);
	
	g_string_append_printf (str, "%.2fpt", l);
	gsf_xml_out_add_cstr_unchecked (xml, id, str->str);
	g_string_free (str, TRUE);
}

static void
gnm_xml_out_add_hex_color (GsfXMLOut *o, char const *id, GnmColor const *c)
{
	char *color;
	g_return_if_fail (c != NULL);

/* FIXME! there should be a difference between white and transparent */

	if ((UINT_RGBA_A (c->go_color) == 0) &&
	    c->gdk_color.red/256 == 0xFF &&
	    c->gdk_color.green/256 == 0xFF &&
	    c->gdk_color.blue/256 == 0xFF)
		gsf_xml_out_add_cstr_unchecked (o, id, "transparent");
	else {
		color = g_strdup_printf ("#%.2x%.2x%.2x", 
					 c->gdk_color.red/256, 
					 c->gdk_color.green/256, 
					 c->gdk_color.blue/256);
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
		prefix = "ND-%i";
		break;
	case 1: 
		hash = state->xl_styles_neg;
		prefix = "ND--%i";
		break;
	default: 
		hash = state->xl_styles_zero;
		prefix = "ND0-%i";
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
	float w = gnm_style_border_get_width (border->line_type);
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
				color->gdk_color.red/256, color->gdk_color.green/256, color->gdk_color.blue/256);
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
					   gnm_style_get_back_color (style));
/* Borders */
	BORDERSTYLE(MSTYLE_BORDER_TOP,FOSTYLE "border-top", STYLE "border-line-width-top", GNMSTYLE "border-line-style-top");
	BORDERSTYLE(MSTYLE_BORDER_BOTTOM,FOSTYLE "border-bottom", STYLE "border-line-width-bottom", GNMSTYLE "border-line-style-bottom");
	BORDERSTYLE(MSTYLE_BORDER_LEFT,FOSTYLE "border-left", STYLE "border-line-width-left", GNMSTYLE "border-line-style-left");
	BORDERSTYLE(MSTYLE_BORDER_RIGHT,FOSTYLE "border-right", STYLE "border-line-width-right", GNMSTYLE "border-line-style-right");
	BORDERSTYLE(MSTYLE_BORDER_REV_DIAGONAL,STYLE "diagonal-bl-tr", STYLE "diagonal-bl-tr-widths", GNMSTYLE "diagonal-bl-tr-line-style");
	BORDERSTYLE(MSTYLE_BORDER_DIAGONAL,STYLE "diagonal-tl-br",  STYLE "diagonal-tl-br-widths", GNMSTYLE "diagonal-tl-br-line-style");
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
	if (gnm_style_is_element_set (style, MSTYLE_ROTATION) && state->with_extension) {
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
					   gnm_style_get_font_color (style));
/* Font Family */
	if (gnm_style_is_element_set (style, MSTYLE_FONT_NAME))
		gsf_xml_out_add_cstr (state->xml, FOSTYLE "font-family",
				      gnm_style_get_font_name (style));


	gsf_xml_out_end_element (state->xml); /* </style:text-properties */	
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

static void
odf_write_style (GnmOOExport *state, GnmStyle const *style, gboolean is_default)
{

	if ((!is_default) && gnm_style_is_element_set (style, MSTYLE_FORMAT)) {
		GOFormat *format = gnm_style_get_format(style);
		if (format != NULL)
			odf_write_style_goformat_name (state, format);
	}


	odf_write_style_cell_properties (state, style);
	odf_write_style_paragraph_properties (state, style);
	odf_write_style_text_properties (state, style);


	
/* MSTYLE_VALIDATION validations need to be written at a different place and time in ODF  */
/* MSTYLE_HLINK hyperlinks can not be attached to styles but need to be attached to the cell content */

/* MSTYLE_CONDITIONS  What are we using these for?  */
}

#undef UNDERLINESPECS
#undef BORDERSTYLE

static const char*
odf_find_style (GnmOOExport *state, GnmStyle const *style)
{
	char const *found = g_hash_table_lookup (state->cell_styles, style);

	if (found == NULL) {
		g_warning("We forgot to export a required style!");
		return "Missing-Style";
	}

	return found;
}

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
odf_save_this_style (GnmStyle *style, G_GNUC_UNUSED gconstpointer dummy, GnmOOExport *state)
{
	char *name = g_strdup_printf ("ACELL-%p", style);
	g_hash_table_insert (state->cell_styles, style, name);
	odf_start_style (state->xml, name, "table-cell");
	odf_write_style (state, style, FALSE);
	gsf_xml_out_end_element (state->xml); /* </style:style */
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

	for (i = 0; i < workbook_sheet_count (state->wb); i++)
		sheet_style_foreach (workbook_sheet_by_index (state->wb, i),
				     (GHFunc) odf_save_this_style,
				     state);
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

	if (ref->a.sheet == NULL)
		g_string_append (out->accum, ":.");
	else
		g_string_append_c (out->accum, ':');

	cellref_as_string (out, &(ref->b), FALSE);
	g_string_append (out->accum, "]");
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

static void
odf_write_empty_cell (GnmOOExport *state, int num, GnmStyle const *style, GnmComment const *cc)
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
		if (cc != NULL)
			odf_write_comment (state, cc);
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
		GnmComment const *cc)
{
	int rows_spanned = 0, cols_spanned = 0;
	gboolean pp = TRUE;

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
		case VALUE_BOOLEAN:
			gsf_xml_out_add_cstr_unchecked (state->xml,
							OFFICE "value-type", "boolean");
			odf_add_bool (state->xml, OFFICE "boolean-value",
				value_get_as_bool (cell->value, NULL));
			break;
		case VALUE_FLOAT:
			gsf_xml_out_add_cstr_unchecked (state->xml,
							OFFICE "value-type", "float");
			gsf_xml_out_add_float (state->xml, OFFICE "value",
					       value_get_as_float
					       (cell->value),
					       10);
			break;

		case VALUE_STRING:
			gsf_xml_out_add_cstr_unchecked (state->xml,
							OFFICE "value-type", "string");
			break;
		case VALUE_ERROR:
			gsf_xml_out_add_cstr_unchecked (state->xml,
							OFFICE "value-type", "string");
			gsf_xml_out_add_cstr (state->xml,
					      OFFICE "string-value",
					      value_peek_string (cell->value));
			break;

		case VALUE_ARRAY:
		case VALUE_CELLRANGE:
		default:
			break;

		}
	}

	if (cc != NULL)
		odf_write_comment (state, cc);
	
	if (cell != NULL && cell->value != NULL) {
		g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
		if ((VALUE_FMT (cell->value) == NULL)
		    || (!VALUE_IS_STRING (cell->value))
		    || (!go_format_is_markup (VALUE_FMT (cell->value)))) {
			char *rendered_string = gnm_cell_get_rendered_text (cell);
			gboolean white_written = TRUE;
			
			if (*rendered_string != '\0') {
				gsf_xml_out_start_element (state->xml, TEXT "p");
				odf_add_chars (state, rendered_string, strlen (rendered_string), 
					       &white_written);
				gsf_xml_out_end_element (state->xml);   /* p */
			}
			g_free (rendered_string);
		} else {
			GString *str = g_string_new (NULL);
			const PangoAttrList * markup;
			
			value_get_as_gstring (cell->value, str, NULL);
			markup = go_format_get_markup (VALUE_FMT (cell->value));
			
			gsf_xml_out_start_element (state->xml, TEXT "p");
			odf_new_markup (state, markup, str->str);
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

static void
odf_write_content_rows (GnmOOExport *state, Sheet const *sheet, int from, int to, 
			G_GNUC_UNUSED int col_from, G_GNUC_UNUSED int col_to, int row_length, 
			GSList **sheet_merges, GnmPageBreaks *pb, G_GNUC_UNUSED GnmStyle **col_styles)
{
	int col, row;
	int n;
	GnmStyleRow sr;

	n = row_length + 2;
	sr.vertical = g_alloca (n * 4 * sizeof (gpointer));
	sr.vertical += 1;
	sr.top		 = sr.vertical + n;
	sr.bottom	 = sr.top + n;
	sr.styles	 = ((GnmStyle const **) (sr.bottom + n));
	sr.start_col = 0;
	sr.end_col = row_length - 1;
	sr.hide_grid = TRUE;

	/* We are currently ignoring col_from and col_to but using them should speed things up.*/

	for (row = from; row < to; row++) {
		ColRowInfo const *ci = sheet_row_get (sheet, row);
		GnmStyle const *null_style = NULL;
		int null_cell = 0;
		int covered_cell = 0;
		GnmCellPos pos;

		pos.row = row;

		sr.row = row;
		sheet_style_get_row (sheet, &sr);

		if (gnm_page_breaks_get_break (pb, row) != GNM_PAGE_BREAK_NONE)
			gsf_xml_out_simple_element (state->xml, 
						    TEXT "soft-page-break", 
						    NULL);

		gsf_xml_out_start_element (state->xml, TABLE "table-row");
		write_row_style (state, ci, sheet);

		for (col = 0; col < row_length; col++) {
			GnmCell *current_cell = sheet_cell_get (sheet, col, row);
			GnmRange const	*merge_range;
			GnmComment const *cc;

			pos.col = col;
			cc = sheet_get_comment (sheet, &pos);
			merge_range = gnm_sheet_merge_is_corner (sheet, &pos);

			if (odf_cell_is_covered (sheet, current_cell, col, row,
						merge_range, sheet_merges)) {
				odf_write_empty_cell (state, null_cell, null_style, NULL);
				null_cell = 0;
				covered_cell++;
				continue;
			}
			if ((merge_range == NULL) && (cc == NULL) &&
			    gnm_cell_is_empty (current_cell)) {
				GnmStyle const *this_style = sr.styles [col];
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
			odf_write_cell (state, current_cell, merge_range, cc);

		}
		odf_write_empty_cell (state, null_cell, null_style, NULL);
		null_cell = 0;
		if (covered_cell > 0)
			odf_write_covered_cell (state, &covered_cell);

		gsf_xml_out_end_element (state->xml);   /* table-row */
	}


}

static void
odf_write_sheet (GnmOOExport *state, Sheet const *sheet)
{
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
	char *val_str = NULL;

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
		type = "number";
		val_str = g_strdup_printf ("%g", cond->count);
	} else if (GNM_FILTER_OP_TYPE_BLANKS  != (cond->op[0] & GNM_FILTER_OP_TYPE_MASK)) {
		type = VALUE_IS_FLOAT (cond->value[0]) ? "number" : "text";
		val_str = value_get_as_string (cond->value[0]);
	}

	gsf_xml_out_start_element (state->xml, TABLE "filter-condition");
	gsf_xml_out_add_int (state->xml, TABLE "field-number", i);
	if (NULL != type) {
		gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "data-type", type);
		gsf_xml_out_add_cstr (state->xml, TABLE "value", val_str);
	}
	gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "operator", op);
	gsf_xml_out_end_element (state->xml); /* </table:filter-condition> */

	g_free (val_str);
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
	if (gnm_date_convention_base (workbook_date_conv (state->wb)) == 1900)
		/* As encouraged by the OpenFormula definition we "compensate" here. */
		gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "date-value", "1899-12-30");
	else
		gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "date-value", "1904-1-1");
	gsf_xml_out_add_cstr_unchecked (state->xml, TABLE "value-type", "date");
	gsf_xml_out_end_element (state->xml); /* </table:null-date> */	
	gsf_xml_out_start_element (state->xml, TABLE "iteration");
	gsf_xml_out_add_float (state->xml, TABLE "maximum-difference", 
			       state->wb->iteration.tolerance, 6);
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
			char *closing;
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
			/* While this should be enough, ODF doesn't want the same format here as in formulas: */
			closing = strrchr(formula, ']');
			if (closing != NULL)
				*closing = '\0';
			gnm_expr_top_unref (texpr);
			gsf_xml_out_add_cstr (state->xml, TABLE "print-ranges", (*formula == '[') ? (formula + 1) : formula);
			g_free (formula);
		}

		odf_write_sheet (state, sheet);
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
	if (xl == NULL) return;
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

	g_hash_table_foreach (state->xl_styles_zero, (GHFunc) odf_write_this_xl_style_zero, state);
	g_hash_table_foreach (state->xl_styles_neg, (GHFunc) odf_write_this_xl_style_neg, state);
	g_hash_table_foreach (state->xl_styles, (GHFunc) odf_write_this_xl_style, state);
	g_hash_table_foreach (state->xl_styles_conditional, (GHFunc) odf_write_this_conditional_xl_style, state);
	
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
	gsf_xml_out_end_element (xml); /* </manifest:manifest> */
	g_object_unref (xml);
}

/**********************************************************************************/

static void
openoffice_file_save_real (GOFileSaver const *fs, IOContext *ioc,
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
	GsfOutfile *outfile = NULL;
	GsfOutput  *child;
	GnmLocale  *locale;
	GError *err;
	unsigned i;
	Sheet *sheet;

	locale  = gnm_push_C_locale ();

	outfile = gsf_outfile_zip_new (output, &err);

	state.with_extension = with_extension;
	state.ioc = ioc;
	state.wbv = wbv;
	state.wb  = wb_view_get_workbook (wbv);
	state.conv = odf_expr_conventions_new ();
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
	state.default_style = sheet_style_default (sheet);
	state.column_default = &sheet->cols.default_style;
	state.row_default = &sheet->rows.default_style;

	for (i = 0 ; i < G_N_ELEMENTS (streams); i++) {
		child = gsf_outfile_new_child_full (outfile, streams[i].name, FALSE,
				/* do not compress the mimetype */
				"compression-level", ((0 == i) ? GSF_ZIP_STORED : GSF_ZIP_DEFLATED),
				NULL);
		if (NULL != child) {
			streams[i].func (&state, child);
			gsf_output_close (child);
			g_object_unref (G_OBJECT (child));
		}
	}

	g_free (state.conv);

	gsf_output_close (GSF_OUTPUT (outfile));
	g_object_unref (G_OBJECT (outfile));

	gnm_pop_C_locale (locale);
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
openoffice_file_save (GOFileSaver const *fs, IOContext *ioc,
		      WorkbookView const *wbv, GsfOutput *output);

G_MODULE_EXPORT void
openoffice_file_save (GOFileSaver const *fs, IOContext *ioc,
		      WorkbookView const *wbv, GsfOutput *output)
{
	openoffice_file_save_real (fs, ioc, wbv, output, FALSE);
}

void
odf_file_save (GOFileSaver const *fs, IOContext *ioc,
		      WorkbookView const *wbv, GsfOutput *output);

G_MODULE_EXPORT void
odf_file_save (GOFileSaver const *fs, IOContext *ioc,
		      WorkbookView const *wbv, GsfOutput *output)
{
	openoffice_file_save_real (fs, ioc, wbv, output, TRUE);
}


