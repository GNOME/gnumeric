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

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <workbook-view.h>
#include <goffice/app/file.h>
#include <goffice/app/io-context.h>
#include <goffice/utils/go-format.h>
#include <goffice/utils/go-units.h>
#include <gnm-format.h>
#include <workbook.h>
#include <workbook-priv.h> /* Workbook::names */
#include <cell.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <sheet-merge.h>
#include <style-color.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <value.h>
#include <str.h>
#include <ranges.h>
#include <mstyle.h>
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
#include <goffice/utils/go-glib-extras.h>
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
#define GNMSTYLE	 "gnm:"  /* We use this for attributes and elements not supported by ODF */

typedef struct {
	GsfXMLOut *xml;
	IOContext *ioc;
	WorkbookView const *wbv;
	Workbook const	   *wb;
	GnmConventions *conv;
	GSList *cell_styles;
} GnmOOExport;

typedef struct {
	int counter;
	char *name;
	GnmStyle const *style;
} cell_styles_t;

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
		to = (to > len) ? len : to; /* Since "to" can be really big! */
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

static void
cell_styles_free (gpointer data)
{
	cell_styles_t *style = data;
	
	g_free (style->name);
	gnm_style_unref (style->style);
	g_free (style);
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

static gint 
odf_compare_style (gconstpointer a, gconstpointer b)
{
	cell_styles_t const *old_style = a;	
	GnmStyle const *new_style = b;

	return !gnm_style_equal (new_style, old_style->style);
}

static void
gnm_xml_out_add_hex_color (GsfXMLOut *o, char const *id, GnmColor const *c)
{
	char *color;
	g_return_if_fail (c != NULL);
	
	color = g_strdup_printf ("#%.2x%.2x%.2x", 
				 c->gdk_color.red/256, c->gdk_color.green/256, c->gdk_color.blue/256);
	gsf_xml_out_add_cstr_unchecked (o, id, color);
	g_free (color);
}

static char *
odf_get_border_format (GnmBorder   *border)
{
	GString *str = g_string_new ("");
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

#define BORDERSTYLE(msbw, msbwstr, msbwstr_wth, msbwstr_gnm) if (gnm_style_is_element_set (style->style, msbw)) { \
	                GnmBorder *border = gnm_style_get_border (style->style, msbw); \
			char *border_style = odf_get_border_format (border); \
			char const *gnm_border_style = odf_get_gnm_border_format (border); \
			gsf_xml_out_add_cstr_unchecked (state->xml, msbwstr, border_style); \
			g_free (border_style); \
                        if (gnm_border_style != NULL) \
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
odf_write_style (GnmOOExport *state, cell_styles_t *style)
{
		odf_start_style (state->xml, style->name, "table-cell");

		gsf_xml_out_start_element (state->xml, STYLE "table-cell-properties");
		if (gnm_style_is_element_set (style->style, MSTYLE_COLOR_BACK))
			gnm_xml_out_add_hex_color (state->xml, FOSTYLE "background-color",
						   gnm_style_get_back_color (style->style));
		BORDERSTYLE(MSTYLE_BORDER_TOP,FOSTYLE "border-top", STYLE "border-line-width-top", GNMSTYLE "border-line-style-top");
		BORDERSTYLE(MSTYLE_BORDER_BOTTOM,FOSTYLE "border-bottom", STYLE "border-line-width-bottom", GNMSTYLE "border-line-style-bottom");
		BORDERSTYLE(MSTYLE_BORDER_LEFT,FOSTYLE "border-left", STYLE "border-line-width-left", GNMSTYLE "border-line-style-left");
		BORDERSTYLE(MSTYLE_BORDER_RIGHT,FOSTYLE "border-right", STYLE "border-line-width-right", GNMSTYLE "border-line-style-right");
		BORDERSTYLE(MSTYLE_BORDER_REV_DIAGONAL,STYLE "diagonal-bl-tr", STYLE "diagonal-bl-tr-widths", GNMSTYLE "diagonal-bl-tr-line-style");
		BORDERSTYLE(MSTYLE_BORDER_DIAGONAL,STYLE "diagonal-tl-br",  STYLE "diagonal-tl-br-widths", GNMSTYLE "diagonal-tl-br-line-style");
		gsf_xml_out_end_element (state->xml); /* </style:table-cell-properties */

		gsf_xml_out_start_element (state->xml, STYLE "text-properties");
		if (gnm_style_is_element_set (style->style, MSTYLE_FONT_BOLD))
			gsf_xml_out_add_int (state->xml, FOSTYLE "font-weight", 
					     gnm_style_get_font_bold (style->style) 
					     ? PANGO_WEIGHT_BOLD 
					     : PANGO_WEIGHT_NORMAL);
		if (gnm_style_is_element_set (style->style, MSTYLE_FONT_ITALIC))
			gsf_xml_out_add_cstr (state->xml, FOSTYLE "font-style", 
					      gnm_style_get_font_italic (style->style) 
					      ? "italic" : "normal");
		if (gnm_style_is_element_set (style->style, MSTYLE_FONT_STRIKETHROUGH)) {
			if (gnm_style_get_font_strike (style->style)) {
				gsf_xml_out_add_cstr (state->xml,  STYLE "text-line-through-type", "single");
				gsf_xml_out_add_cstr (state->xml, STYLE "text-line-through-style", "solid");
			} else {
				gsf_xml_out_add_cstr (state->xml,  STYLE "text-line-through-type", "none");
				gsf_xml_out_add_cstr (state->xml, STYLE "text-line-through-style", "none");
			}}
		if (gnm_style_is_element_set (style->style, MSTYLE_FONT_STRIKETHROUGH))
			switch (gnm_style_get_font_uline (style->style)) {
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
		if (gnm_style_is_element_set (style->style, MSTYLE_FONT_SCRIPT))		
			switch (gnm_style_get_font_script (style->style)) {
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
		if (gnm_style_is_element_set (style->style, MSTYLE_FONT_SIZE))		
			gsf_xml_out_add_int (state->xml, FOSTYLE "font-size",
					     gnm_style_get_font_size (style->style));
		if (gnm_style_is_element_set (style->style, MSTYLE_FONT_COLOR))
			gnm_xml_out_add_hex_color (state->xml, FOSTYLE "color",
						   gnm_style_get_font_color (style->style));
		if (gnm_style_is_element_set (style->style, MSTYLE_FONT_NAME))
			gsf_xml_out_add_cstr (state->xml, FOSTYLE "font-family",
					      gnm_style_get_font_name (style->style));
		gsf_xml_out_end_element (state->xml); /* </style:text-properties */

		gsf_xml_out_end_element (state->xml); /* </style:style */
}

#undef UNDERLINESPECS
#undef BORDERSTYLE

static const char*
odf_find_style (GnmOOExport *state, GnmStyle const *style, gboolean write)
{
	cell_styles_t *new_style;
	GSList *found = g_slist_find_custom (state->cell_styles, style, odf_compare_style);

	if (found) {
		new_style = found->data;
		return new_style->name;
	} else {
		new_style = g_new0 (cell_styles_t,1);
		new_style->style = style;
		gnm_style_ref (style);
		new_style->counter = g_slist_length (state->cell_styles);
		new_style->name = g_strdup_printf ("ACELL-%i", new_style->counter);
		state->cell_styles = g_slist_prepend (state->cell_styles, new_style);
		if (write)
			odf_write_style (state, new_style);
		return new_style->name;
	}
}

static void
odf_load_required_automatic_styles (GnmOOExport *state)
{
        /* We have to figure out which automatic styles we need   */
	/* This is really annoying since we have to scan through  */
	/* all cells. If we could store these cells in styles.xml */
	/* we could create them as we need them, but these styles */
	/* have to go into the beginning of content.xml.          */
	int j;

	for (j = 0; j < workbook_sheet_count (state->wb); j++) {
		Sheet const *sheet = workbook_sheet_by_index (state->wb, j);
		int max_cols = gnm_sheet_get_max_cols (sheet);
		int max_rows = gnm_sheet_get_max_rows (sheet);
		GnmStyle **col_styles = g_new (GnmStyle *, max_cols);
		GnmRange  extent;
		int i, col, row;

		extent = sheet_get_extent (sheet, FALSE);
		sheet_style_get_extent (sheet, &extent, col_styles);
		
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
		
		for (row = extent.start.row; row <= extent.end.row; row++) {
			for (col = extent.start.col; col <= extent.end.col; col++) {
				GnmCell *current_cell = sheet_cell_get (sheet, col, row);
				if (current_cell != NULL) {
					GnmStyle const *style;
					
					if (gnm_cell_is_empty (current_cell))
						continue;
					
					style = sheet_style_get (sheet, col, row);
					if (style != NULL)
						odf_find_style (state, style, TRUE);
				}
			}
		}
		g_free (col_styles);
	}

	return;
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

	odf_load_required_automatic_styles (state);
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
odf_write_empty_cell (GnmOOExport *state, int *num)
{
	if (*num > 0) {
		gsf_xml_out_start_element (state->xml, TABLE "table-cell");
		if (*num > 1)
			gsf_xml_out_add_int (state->xml,
					     TABLE "number-columns-repeated",
					     *num);
		gsf_xml_out_end_element (state->xml);   /* table-cell */
		*num = 0;
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
		gboolean pp = TRUE;
		GnmStyle const *style = gnm_cell_get_style (cell);

		if (style) {
			char const * name = odf_find_style (state, style, FALSE);
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

		if (cell->value != NULL) {
			g_object_get (G_OBJECT (state->xml), "pretty-print", &pp, NULL);
			g_object_set (G_OBJECT (state->xml), "pretty-print", FALSE, NULL);
			if ((VALUE_FMT (cell->value) == NULL)
			    || (!VALUE_IS_STRING (cell->value))
			    || (!go_format_is_markup (VALUE_FMT (cell->value)))) {
				char *rendered_string = gnm_cell_get_rendered_text (cell);
				gboolean white_written = TRUE;
				
				if (*rendered_string != '\0') {
					gsf_xml_out_start_element (state->xml, TEXT "p");
					odf_add_chars (state, rendered_string, 
						       strlen (rendered_string), 
						       &white_written);
					gsf_xml_out_end_element (state->xml);   /* p */
				}
				
				g_free (rendered_string);
			} else {
				GString *str = g_string_new ("");
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
	}

	if (cc != NULL) {
		char const *author;
		author = cell_comment_author_get (cc);

		gsf_xml_out_start_element (state->xml, OFFICE "annotation");
		if (author != NULL) {
			gsf_xml_out_start_element (state->xml, DUBLINCORE "creator");
			gsf_xml_out_add_cstr (state->xml, NULL, author);
			gsf_xml_out_end_element (state->xml); /*  DUBLINCORE "creator" */;
		}
		gsf_xml_out_add_cstr (state->xml, NULL, cell_comment_text_get (cc));
		gsf_xml_out_end_element (state->xml); /*  OFFICE "annotation" */
	}

	gsf_xml_out_end_element (state->xml);   /* table-cell */
}

static void
odf_write_sheet (GnmOOExport *state, Sheet const *sheet)
{
	int max_cols = gnm_sheet_get_max_cols (sheet);
	int max_rows = gnm_sheet_get_max_rows (sheet);
	GnmStyle **col_styles = g_new (GnmStyle *, max_cols);
	GnmRange  extent;
	int i, col, row;
	int null_cell;
	int covered_cell;
	GnmCellPos pos;
	GSList *sheet_merges = NULL;

	extent = sheet_get_extent (sheet, FALSE);
	sheet_style_get_extent (sheet, &extent, col_styles);

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

	gsf_xml_out_start_element (state->xml, TABLE "table-column");
	gsf_xml_out_add_int (state->xml, TABLE "number-columns-repeated",
			     extent.end.col + 1);
	gsf_xml_out_end_element (state->xml); /* table-column */

	if (extent.start.row > 0) {
		/* We need to write a bunch of empty rows !*/
		gsf_xml_out_start_element (state->xml, TABLE "table-row");
		if (extent.start.row > 1)
			gsf_xml_out_add_int (state->xml, TABLE "number-rows-repeated",
					     extent.start.row);
		gsf_xml_out_start_element (state->xml, TABLE "table-cell");
		gsf_xml_out_end_element (state->xml);   /* table-cell */
		gsf_xml_out_end_element (state->xml);   /* table-row */
	}

	for (row = extent.start.row; row <= extent.end.row; row++) {
		null_cell = extent.start.col;
		covered_cell = 0;
		pos.row = row;

		gsf_xml_out_start_element (state->xml, TABLE "table-row");

		for (col = extent.start.col; col <= extent.end.col; col++) {
			GnmCell *current_cell = sheet_cell_get (sheet, col, row);
			GnmRange const	*merge_range;
			GnmComment const *cc;

			pos.col = col;
			cc = sheet_get_comment (sheet, &pos);
			merge_range = gnm_sheet_merge_is_corner (sheet, &pos);

			if (odf_cell_is_covered (sheet, current_cell, col, row,
						merge_range, &sheet_merges)) {
				if (null_cell >0)
					odf_write_empty_cell (state, &null_cell);
				covered_cell++;
				continue;
			}
			if ((merge_range == NULL) && (cc == NULL) &&
			    gnm_cell_is_empty (current_cell)) {
				if (covered_cell > 0)
					odf_write_covered_cell (state, &covered_cell);
				null_cell++;
				continue;
			}

			if (null_cell > 0)
				odf_write_empty_cell (state, &null_cell);
			if (covered_cell > 0)
				odf_write_covered_cell (state, &covered_cell);
			odf_write_cell (state, current_cell, merge_range, cc);

		}
		if (null_cell > 0)
			odf_write_empty_cell (state, &null_cell);
		if (covered_cell > 0)
			odf_write_covered_cell (state, &covered_cell);

		gsf_xml_out_end_element (state->xml);   /* table-row */
	}

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
odf_write_content (GnmOOExport *state, GsfOutput *child)
{
	int i;
	gboolean has_autofilters = FALSE;

	state->xml = gsf_xml_out_new (child);
	gsf_xml_out_set_doc_type (state->xml, "\n");
	gsf_xml_out_start_element (state->xml, OFFICE "document-content");

	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version", "1.0");

	gsf_xml_out_simple_element (state->xml, OFFICE "scripts", NULL);

	gsf_xml_out_start_element (state->xml, OFFICE "font-face-decls");
	gsf_xml_out_end_element (state->xml); /* </office:font-face-decls> */

	gsf_xml_out_start_element (state->xml, OFFICE "automatic-styles");
	odf_write_table_styles (state);
	odf_write_character_styles (state);
	gsf_xml_out_end_element (state->xml); /* </office:automatic-styles> */

	gsf_xml_out_start_element (state->xml, OFFICE "body");
	gsf_xml_out_start_element (state->xml, OFFICE "spreadsheet");
	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet const *sheet = workbook_sheet_by_index (state->wb, i);
		char *style_name;

		gsf_xml_out_start_element (state->xml, TABLE "table");
		gsf_xml_out_add_cstr (state->xml, TABLE "name", sheet->name_unquoted);

		style_name = table_style_name (sheet);
		gsf_xml_out_add_cstr (state->xml, TABLE "style-name", style_name);
		g_free (style_name);

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
odf_write_styles (GnmOOExport *state, GsfOutput *child)
{
	int i;

	state->xml = gsf_xml_out_new (child);
	gsf_xml_out_start_element (state->xml, OFFICE "document-styles");
	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version", "1.0");
	gsf_xml_out_end_element (state->xml); /* </office:document-styles> */
	g_object_unref (state->xml);
	state->xml = NULL;
}

/*****************************************************************************/

static void
odf_write_meta (GnmOOExport *state, GsfOutput *child)
{
	GsfXMLOut *xml = gsf_xml_out_new (child);
	gsf_opendoc_metadata_write (xml,
		go_doc_get_meta_data (GO_DOC (state->wb)));
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
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version", "1.0");
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

void
openoffice_file_save (GOFileSaver const *fs, IOContext *ioc,
		      WorkbookView const *wbv, GsfOutput *output);

G_MODULE_EXPORT void
openoffice_file_save (GOFileSaver const *fs, IOContext *ioc,
		      WorkbookView const *wbv, GsfOutput *output)
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

	locale  = gnm_push_C_locale ();

	outfile = gsf_outfile_zip_new (output, &err);

	state.ioc = ioc;
	state.wbv = wbv;
	state.wb  = wb_view_get_workbook (wbv);
	state.conv = odf_expr_conventions_new ();
	state.cell_styles = NULL;
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
	go_slist_free_custom (state.cell_styles, cell_styles_free);
}
