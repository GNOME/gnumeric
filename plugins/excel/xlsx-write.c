/*
 * xlsx-write.c : export MS Office Open xlsx files.
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
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
#include <goffice/goffice.h>

#include "ms-excel-write.h"
#include "xlsx-utils.h"

#include <parse-util.h>
#include <workbook.h>
#include <workbook-priv.h>
#include <workbook-view.h>
#include <sheet.h>
#include <sheet-style.h>
#include <sheet-view.h>
#include <sheet-filter.h>
#include <sheet-filter-combo.h>
#include <ranges.h>
#include <value.h>
#include <cell.h>
#include <expr.h>
#include <func.h>
#include <style-color.h>
#include <validation.h>
#include <hlink.h>
#include <input-msg.h>
#include <print-info.h>
#include <gutils.h>
#include <sheet-object.h>
#include <sheet-object-cell-comment.h>
#include <sheet-object-graph.h>
#include <sheet-object-widget.h>
#include <sheet-object-image.h>
#include <gnm-so-line.h>
#include <gnm-so-filled.h>
#include <graph.h>
#include <style-border.h>
#include <style-conditions.h>
#include <gutils.h>
#include <expr-name.h>

#include <go-val.h>

#include <gsf/gsf-output.h>
#include <gsf/gsf-outfile.h>
#include <gsf/gsf-outfile-zip.h>
#include <gsf/gsf-open-pkg-utils.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-libxml.h>
#include <glib/gi18n-lib.h>
#include <gsf/gsf-meta-names.h>
#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-docprop-vector.h>
#include <gsf/gsf-timestamp.h>
#include <gmodule.h>
#include <string.h>

#define NUM_FORMAT_BASE 100

enum {
	ECMA_376_2006 = 1,
	ECMA_376_2008 = 2
};

static char const *ns_ss	 = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
static char const *ns_ss_drawing = "http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing";
static char const *ns_docprops_core_cp       = "http://schemas.openxmlformats.org/package/2006/metadata/core-properties";
static char const *ns_docprops_core_dc       = "http://purl.org/dc/elements/1.1/";
static char const *ns_docprops_core_dcmitype = "http://purl.org/dc/dcmitype/";
static char const *ns_docprops_core_dcterms  = "http://purl.org/dc/terms/";
static char const *ns_docprops_core_xsi      = "http://www.w3.org/2001/XMLSchema-instance";
static char const *ns_docprops_extended      = "http://schemas.openxmlformats.org/officeDocument/2006/extended-properties";
static char const *ns_docprops_extended_vt   = "http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes";
static char const *ns_docprops_custom        = "http://schemas.openxmlformats.org/officeDocument/2006/custom-properties";
static char const *ns_drawing	 = "http://schemas.openxmlformats.org/drawingml/2006/main";
static char const *ns_chart	 = "http://schemas.openxmlformats.org/drawingml/2006/chart";
static char const *ns_vml	 = "urn:schemas-microsoft-com:vml";
static char const *ns_leg_office = "urn:schemas-microsoft-com:office:office";
static char const *ns_leg_excel	 = "urn:schemas-microsoft-com:office:excel";

static char const *ns_gnm_ext = "http://www.gnumeric.org/ext/spreadsheetml";

static char const *ns_rel	 = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
static char const *ns_rel_hlink	 = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
static char const *ns_rel_draw	 = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
static char const *ns_rel_leg_draw = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing";
static char const *ns_rel_chart	 = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart";
static char const *ns_rel_com	 = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments";
static char const *ns_rel_image	 = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";


typedef struct {
	unsigned int count;
	GsfOutfile *parent;
	GsfOutfile *dir;
	const char *name;
} XLSXDir;

static void
xlsx_dir_init (XLSXDir *dir, GsfOutfile *parent, const char *name)
{
	dir->count = 0;
	dir->parent = parent;
	dir->dir = NULL;
	dir->name = name;
}

static void
xlsx_dir_close (XLSXDir *dir)
{
	if (dir->dir) {
		gsf_output_close (GSF_OUTPUT (dir->dir));
		g_object_unref (dir->dir);
		dir->dir = NULL;
	}
}

static GsfOutfile *
xlsx_dir_get (XLSXDir *dir)
{
	if (!dir->dir) {
		char *debug_name = g_strdup_printf ("xlsx directory %s", dir->name);
		dir->dir = (GsfOutfile *)gsf_outfile_new_child (dir->parent, dir->name, TRUE);
		go_debug_check_finalized (dir->dir, debug_name);
		g_free (debug_name);
	}
	return dir->dir;
}



typedef struct {
	XLExportBase base;

	gint             version;
	gboolean         with_extension;

	Sheet const	*sheet;
	GHashTable	*shared_string_hash;
	GPtrArray	*shared_string_array;
	GHashTable	*styles_hash;
	GPtrArray	*styles_array;
	GHashTable	*dxfs_hash;
	GPtrArray	*dxfs_array;
	GnmConventions	*convs;
	GOIOContext	*io_context;
	GHashTable      *axids;

	GsfOutfile	*xl_dir;
	XLSXDir         sheet_dir;
	XLSXDir         chart_dir, drawing_dir, legacy_drawing_dir;
	XLSXDir         media_dir, pivotCache_dir, pivotTable_dir;
	unsigned comment;
	unsigned drawing_elem_id;
	GOFormat *date_fmt;

	int custom_prop_id;
} XLSXWriteState;

typedef struct {
	XLSXWriteState	*state;
	GsfXMLOut	*xml;
} XLSXClosure;

typedef struct {
	int code;
	int width_mm;
	gdouble width;
	gdouble height;
	GtkUnit unit;
} XLSXPaperDefs;

static void
xlsx_add_bool (GsfXMLOut *xml, char const *id, gboolean val)
{
	gsf_xml_out_add_cstr_unchecked (xml, id, val ? "1" : "0");
}
static void
xlsx_add_rgb (GsfXMLOut *xml, char const *id, GOColor c)
{
	char buf [3 * 4 * sizeof (unsigned int) + 1];
	sprintf (buf, "%02X%02X%02X%02X",
		 GO_COLOR_UINT_A (c), GO_COLOR_UINT_R (c),
		 GO_COLOR_UINT_G (c), GO_COLOR_UINT_B (c));
	gsf_xml_out_add_cstr_unchecked (xml, id, buf);
}
static void
xlsx_add_pos (GsfXMLOut *xml, char const *id, GnmCellPos const *pos)
{
	gsf_xml_out_add_cstr_unchecked (xml, id,
		cellpos_as_string (pos));
}
static void
xlsx_add_range (GsfXMLOut *xml, char const *id, GnmRange const *range)
{
	gsf_xml_out_add_cstr_unchecked (xml, id,
		range_as_string (range));
}
static void
xlsx_add_range_list (GsfXMLOut *xml, char const *id, GSList const *ranges)
{
	GString *accum = g_string_new (NULL);

	for (; NULL != ranges ; ranges = ranges->next) {
		g_string_append (accum, range_as_string (ranges->data));
		if (NULL != ranges->next)
			g_string_append_c (accum, ' ');
	}

	gsf_xml_out_add_cstr_unchecked (xml, id, accum->str);
	g_string_free (accum, TRUE);
}
static void
xlsx_add_pt (GsfXMLOut *xml, char const *id, double l)
{
	GString *str = g_string_new (NULL);

	g_string_append_printf (str, "%.2fpt", l);
	gsf_xml_out_add_cstr_unchecked (xml, id, str->str);
	g_string_free (str, TRUE);
}

/****************************************************************************/

#define N_PREDEFINED_FILLS (G_N_ELEMENTS (pre_def_fills) - 1)

static char const * const pats[] = {
	"solid",            /* 1 */
	"darkGray",         /* 2 */
	"mediumGray",       /* 3 */
	"lightGray",        /* 4 */
	"gray125",          /* 5 */
	"gray0625",         /* 6 */
	"darkHorizontal",   /* 7 */
	"darkVertical",     /* 8 */
	"darkUp",           /* 9 */
	"darkDown",         /* 10 */
	"darkGrid",         /* 11 */
	"darkTrellis",      /* 12 */
	"lightHorizontal",  /* 13 */
	"lightVertical",    /* 14 */
	"lightDown",        /* 15 */
	"lightUp",          /* 16 */
	"lightGrid",        /* 17 */
	"lightTrellis",     /* 18 */
	/* Not in Excel */
	"lightVertical",    /* 19 */
	"darkHorizontal",   /* 20 */
	"lightGray",        /* 21 */
	"lightGray",        /* 22 */
	"darkGray",         /* 23 */
	"solid",            /* 24 */
};

static char const * const pre_def_fills[] = {
	"none",
	"gray125",
	NULL
};

static void
xlsx_write_predefined_fills (GsfXMLOut *xml)
{
	char const * const *f = pre_def_fills;

	while (*f != NULL) {
		gsf_xml_out_start_element (xml, "fill");
		gsf_xml_out_start_element (xml, "patternFill");
		gsf_xml_out_add_cstr_unchecked (xml, "patternType", *f++);
		gsf_xml_out_end_element (xml);
		gsf_xml_out_end_element (xml);
	}
}

static gint
xlsx_find_predefined_fill (GnmStyle const *style)
{
	gboolean pattern_is_std =
		(!gnm_style_is_element_set (style, MSTYLE_COLOR_PATTERN) ||
		 gnm_style_get_pattern_color (style)->go_color == GO_COLOR_BLACK);
	gboolean back_is_std =
		(!gnm_style_is_element_set (style, MSTYLE_COLOR_BACK) ||
		 gnm_style_get_back_color (style)->go_color == GO_COLOR_WHITE);

	if (gnm_style_is_element_set (style, MSTYLE_PATTERN) &&
	    gnm_style_get_pattern (style) == 0 &&
	    pattern_is_std &&
	    back_is_std)
		return 0;
	if (gnm_style_is_element_set (style, MSTYLE_PATTERN) &&
	    gnm_style_get_pattern (style) == 5 &&
	    pattern_is_std &&
	    back_is_std
	    )
		return 1;

	return -1;
}

/****************************************************************************/

static void
xlsx_write_rich_text (GsfXMLOut *xml, char const *text, PangoAttrList *attrs,
		      gboolean allow_xml_space)
{
	PangoAttrIterator *iter;
	int start, end, max;

	if (attrs == NULL) {
		gsf_xml_out_start_element (xml, "t");
		gsf_xml_out_add_cstr (xml, NULL, text);
		gsf_xml_out_end_element (xml); /* </t> */
		return;
	}

	max = strlen (text);
	iter = pango_attr_list_get_iterator (attrs);
	do {
		PangoAttribute *attr;
		GOFontScript fs = GO_FONT_SCRIPT_STANDARD;

		gsf_xml_out_start_element (xml, "r");
		gsf_xml_out_start_element (xml, "rPr");

		attr = pango_attr_iterator_get (iter, PANGO_ATTR_FAMILY);
		if (attr) {
			gsf_xml_out_start_element (xml, "rFont");
			gsf_xml_out_add_cstr_unchecked (xml, "val", ((PangoAttrString *) attr)->value);
			gsf_xml_out_end_element (xml); /* </rFont> */
		}

		attr = pango_attr_iterator_get (iter, PANGO_ATTR_WEIGHT);
		if (attr) {
			gsf_xml_out_start_element (xml, "b");
			gsf_xml_out_add_cstr_unchecked (xml, "val", ((PangoAttrInt *) attr)->value > PANGO_WEIGHT_NORMAL ? "true": "false");
			gsf_xml_out_end_element (xml); /* </b> */
		}

		attr = pango_attr_iterator_get (iter, PANGO_ATTR_STYLE);
		if (attr) {
			gsf_xml_out_start_element (xml, "i");
			gsf_xml_out_add_cstr_unchecked (xml, "val", ((PangoAttrInt *) attr)->value != PANGO_STYLE_NORMAL ? "true": "false");
			gsf_xml_out_end_element (xml); /* </i> */
		}

		attr = pango_attr_iterator_get (iter, PANGO_ATTR_STRIKETHROUGH);
		if (attr) {
			gsf_xml_out_start_element (xml, "strike");
			gsf_xml_out_add_cstr_unchecked (xml, "val", ((PangoAttrInt *) attr)->value ? "true": "false");
			gsf_xml_out_end_element (xml); /* </strike> */
		}

		attr = pango_attr_iterator_get (iter, PANGO_ATTR_FOREGROUND);
		if (attr) {
			PangoColor *color = &((PangoAttrColor *) attr)->color;
			char *buf = g_strdup_printf("ff%02x%02x%02x", color->red >> 8, color->green >> 8, color->blue >> 8);
			gsf_xml_out_start_element (xml, "color");
			gsf_xml_out_add_cstr_unchecked (xml, "rgb", buf);
			gsf_xml_out_end_element (xml); /* </color> */
			g_free (buf);
		}

		attr = pango_attr_iterator_get (iter, PANGO_ATTR_SIZE);
		if (attr) {
			gsf_xml_out_start_element (xml, "sz");
			gsf_xml_out_add_uint (xml, "val", ((PangoAttrInt *) attr)->value / PANGO_SCALE);
			gsf_xml_out_end_element (xml); /* </sz> */
		}

		attr = pango_attr_iterator_get (iter, PANGO_ATTR_UNDERLINE);
		if (attr) {
			PangoUnderline u = ((PangoAttrInt *) attr)->value;
			gsf_xml_out_start_element (xml, "u");
			switch (u) {
			case PANGO_UNDERLINE_NONE:
			default:
				gsf_xml_out_add_cstr_unchecked (xml, "val", "none");
				break;
			case PANGO_UNDERLINE_ERROR:
				/* not supported by OpenXML */
				/* Fall through. */
			case PANGO_UNDERLINE_SINGLE:
				gsf_xml_out_add_cstr_unchecked (xml, "val", "single");
				break;
			case PANGO_UNDERLINE_DOUBLE:
				gsf_xml_out_add_cstr_unchecked (xml, "val", "double");
				break;
			case PANGO_UNDERLINE_LOW:
				gsf_xml_out_add_cstr_unchecked (xml, "val", "singleAccounting");
				break;
			}
			gsf_xml_out_end_element (xml); /* </u> */
		}

		attr = pango_attr_iterator_get (iter, go_pango_attr_superscript_get_attr_type ());
		if (attr && ((PangoAttrInt *) attr)->value)
			fs = GO_FONT_SCRIPT_SUPER;
		attr = pango_attr_iterator_get (iter, go_pango_attr_subscript_get_attr_type ());
		if (attr && ((PangoAttrInt *) attr)->value)
			fs = GO_FONT_SCRIPT_SUB;
		if (fs != GO_FONT_SCRIPT_STANDARD) {
			const char *va = (fs == GO_FONT_SCRIPT_SUB)
				? "subscript"
				: "superscript";
			gsf_xml_out_start_element (xml, "vertAlign");
			gsf_xml_out_add_cstr_unchecked (xml, "val", va);
			gsf_xml_out_end_element (xml); /* </vertAlign> */
		}

		gsf_xml_out_end_element (xml); /* </rPr> */
		gsf_xml_out_start_element (xml, "t");
		pango_attr_iterator_range (iter, &start, &end);
		if (end > max)
		    end = max;
		if (start < end) {
			char *buf = g_strndup (text + start, end - start);
			if (allow_xml_space) {
				const char *p;
				gboolean has_space = FALSE;
				for (p = buf; *p; p = g_utf8_next_char (p)) {
					if (g_unichar_isspace (g_utf8_get_char (p))) {
						has_space = TRUE;
						break;
					}
				}
				if (has_space)
					gsf_xml_out_add_cstr_unchecked
						(xml, "xml:space", "preserve");
			}
			gsf_xml_out_add_cstr (xml, NULL, buf);
			g_free (buf);
		}
		gsf_xml_out_end_element (xml); /* </t> */
		gsf_xml_out_end_element (xml); /* </r> */
	} while (pango_attr_iterator_next (iter));
	pango_attr_iterator_destroy (iter);
}

static int
xlsx_shared_string (XLSXWriteState *state, GnmValue const *v)
{
	gpointer tmp;
	int i;

	g_return_val_if_fail (VALUE_IS_STRING (v), -1);

	if (g_hash_table_lookup_extended (state->shared_string_hash,
					  v, NULL, &tmp))
		i = GPOINTER_TO_INT (tmp);
	else {
		GnmValue *v2 = value_dup (v);

		if (VALUE_FMT (v2) && !go_format_is_markup (VALUE_FMT (v2))) {
			value_set_fmt (v2, NULL);
			i = xlsx_shared_string (state, v2);
			value_release (v2);
		} else {
			i = state->shared_string_array->len;
			g_ptr_array_add (state->shared_string_array, v2);
			g_hash_table_insert (state->shared_string_hash,
					     v2, GINT_TO_POINTER (i));
		}
	}

	return i;
}

static void
xlsx_write_shared_strings (XLSXWriteState *state, GsfOutfile *wb_part)
{
	const unsigned N = state->shared_string_array->len;
	unsigned i;
	GsfOutput *part;
	GsfXMLOut *xml;

	if (N == 0)
		return;

	part = gsf_outfile_open_pkg_add_rel
		(state->xl_dir, "sharedStrings.xml",
		 "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml",
		 wb_part,
		 "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings");
	xml = gsf_xml_out_new (part);

	gsf_xml_out_start_element (xml, "sst");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	gsf_xml_out_add_int (xml, "uniqueCount", N);
	gsf_xml_out_add_int (xml, "count", N);

	for (i = 0; i < N; i++) {
		GnmValue const *val =
			g_ptr_array_index (state->shared_string_array, i);
		PangoAttrList *attrs = VALUE_FMT (val)
			? (PangoAttrList *)go_format_get_markup (VALUE_FMT (val))
			: NULL;
		gsf_xml_out_start_element (xml, "si");
		xlsx_write_rich_text (xml,
				      value_peek_string (val),
				      attrs,
				      FALSE);
		gsf_xml_out_end_element (xml); /* </si> */
	}

	gsf_xml_out_end_element (xml); /* </sst> */

	g_object_unref (xml);
	gsf_output_close (part);
	g_object_unref (part);
}

static void
xlsx_load_buildin_num_formats (GHashTable *hash)
{
	static char const * const std_builtins[] = {
		/* 0 */	 "General",
		/* 1 */	 "0",
		/* 2 */	 "0.00",
		/* 3 */	 "#,##0",
		/* 4 */	 "#,##0.00",
		/* 5 */	 NULL,
		/* 6 */	 NULL,
		/* 7 */	 NULL,
		/* 8 */	 NULL,
		/* 9 */  "0%",
		/* 10 */ "0.00%",
		/* 11 */ "0.00E+00",
		/* 12 */ "# ?/?",
		/* 13 */ "# ?""?/?""?",	/* silly trick to avoid using a trigraph */
		/* 14 */ NULL, // Locale version of "mm-dd-yy"
		/* 15 */ "d-mmm-yy",
		/* 16 */ "d-mmm",
		/* 17 */ "mmm-yy",
		/* 18 */ "h:mm AM/PM",
		/* 19 */ "h:mm:ss AM/PM",
		/* 20 */ "h:mm",
		/* 21 */ "h:mm:ss",
		/* 22 */ "m/d/yy h:mm",
		/* 23 */ NULL,
		/* 24 */ NULL,
		/* 25 */ NULL,
		/* 26 */ NULL,
		/* 27 */ NULL,
		/* 28 */ NULL,
		/* 29 */ NULL,
		/* 30 */ NULL,
		/* 31 */ NULL,
		/* 32 */ NULL,
		/* 33 */ NULL,
		/* 34 */ NULL,
		/* 35 */ NULL,
		/* 36 */ NULL,
		/* 37 */ "#,##0 ;(#,##0)",
		/* 38 */ "#,##0 ;[Red](#,##0)",
		/* 39 */ "#,##0.00;(#,##0.00)",
		/* 40 */ "#,##0.00;[Red](#,##0.00)",
		/* 41 */ NULL,
		/* 42 */ NULL,
		/* 43 */ NULL,
		/* 44 */ NULL,
		/* 45 */ "mm:ss",
		/* 46 */ "[h]:mm:ss",
		/* 47 */ "mmss.0",
		/* 48 */ "##0.0E+0",
		/* 49 */ "@"
	};
	unsigned int i;
	GOFormat const *fmt;

	g_return_if_fail (NUM_FORMAT_BASE > (int) G_N_ELEMENTS (std_builtins));

	for (i = 1; i < G_N_ELEMENTS (std_builtins); i++)
		if (std_builtins[i] != NULL)
			g_hash_table_insert (hash, g_strdup (std_builtins[i]), GUINT_TO_POINTER (i));

	// Format 14 is locale dependent.  ms-excel-read.c suggests that maybe
	// 15 should be too, but I cannot verify that anywhere.
	fmt = go_format_new_magic (GO_FORMAT_MAGIC_SHORT_DATE);
	g_hash_table_insert (hash, g_strdup (go_format_as_XL (fmt)),
			     GUINT_TO_POINTER (14));
	go_format_unref (fmt);
}

static void
xlsx_write_num_format (XLSXWriteState *state, GsfXMLOut *xml,
		       GOFormat const *format, int id)
{
	gsf_xml_out_start_element (xml, "numFmt");
	gsf_xml_out_add_cstr (xml, "formatCode", go_format_as_XL (format));
	gsf_xml_out_add_int (xml, "numFmtId", id);
	gsf_xml_out_end_element (xml);
}

static GHashTable *
xlsx_write_num_formats (XLSXWriteState *state, GsfXMLOut *xml)
{
	GHashTable *hash = g_hash_table_new_full
		(g_str_hash,  g_str_equal, g_free, NULL);
	GHashTable *num_format_hash = g_hash_table_new
		(g_direct_hash, g_direct_equal);
	unsigned int i, count;
	GPtrArray *num_format_array = g_ptr_array_new ();

	xlsx_load_buildin_num_formats (hash);

	for (i = 0 ; i < state->styles_array->len ; i++) {
		GnmStyle const *style = g_ptr_array_index (state->styles_array, i);
		GOFormat const *format;
		char const *xl;
		gpointer tmp;

		if (!gnm_style_is_element_set (style, MSTYLE_FORMAT))
			continue;
		format = gnm_style_get_format (style);
		if (go_format_is_general (format))
			continue;
		xl = go_format_as_XL (format);
		tmp = g_hash_table_lookup (hash, xl);
		if (tmp == NULL) {
			tmp = GUINT_TO_POINTER (num_format_array->len + NUM_FORMAT_BASE);
			g_hash_table_insert (hash, g_strdup (xl), tmp);
			g_ptr_array_add (num_format_array, (gpointer)format);
		}
		g_hash_table_insert (num_format_hash, (gpointer) style, tmp);
	}

	count = num_format_array->len;
	if (count != 0) {
		gsf_xml_out_start_element (xml, "numFmts");
		gsf_xml_out_add_int (xml, "count", count);
		for (i = 0; i < count; i++) {
			GOFormat const *format =
				g_ptr_array_index (num_format_array, i);
			xlsx_write_num_format (state, xml, format,
					       i + NUM_FORMAT_BASE);
		}
		gsf_xml_out_end_element (xml);
	}
	g_ptr_array_free (num_format_array, TRUE);
	g_hash_table_destroy (hash);
	return num_format_hash;
}

static void
xlsx_write_color_element (GsfXMLOut *xml, char const *id, GOColor color)
{
	gsf_xml_out_start_element (xml, id);
	xlsx_add_rgb (xml, "rgb", color);
	gsf_xml_out_end_element (xml);
}

static gint
xlsx_find_font (GnmStyle const *style, GPtrArray  *styles)
{
	unsigned i;
	for (i = 0 ; i < styles->len ; i++) {
		GnmStyle const *old_style = g_ptr_array_index (styles, i);
		if (style == old_style ||
		    (gnm_style_equal_elem (style, old_style, MSTYLE_FONT_BOLD) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_FONT_ITALIC) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_FONT_UNDERLINE) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_FONT_COLOR) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_FONT_SIZE) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_FONT_SCRIPT) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_FONT_STRIKETHROUGH) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_FONT_NAME)))
			return (gint) i;
	}
	return -1;
}

static gboolean
xlsx_has_font_style (GnmStyle const *style)
{
	return (gnm_style_is_element_set (style, MSTYLE_FONT_BOLD) ||
		gnm_style_is_element_set (style, MSTYLE_FONT_ITALIC) ||
		gnm_style_is_element_set (style, MSTYLE_FONT_UNDERLINE) ||
		gnm_style_is_element_set (style, MSTYLE_FONT_COLOR) ||
		gnm_style_is_element_set (style, MSTYLE_FONT_NAME) ||
		gnm_style_is_element_set (style, MSTYLE_FONT_SCRIPT) ||
		gnm_style_is_element_set (style, MSTYLE_FONT_SIZE) ||
		gnm_style_is_element_set (style, MSTYLE_FONT_STRIKETHROUGH));
}

static void
xlsx_write_font (XLSXWriteState *state, GsfXMLOut *xml, GnmStyle const *style)
{
	gsf_xml_out_start_element (xml, "font");
	if (gnm_style_is_element_set (style, MSTYLE_FONT_BOLD)) {
		gsf_xml_out_start_element (xml, "b");
		xlsx_add_bool (xml, "val", gnm_style_get_font_bold (style));
		gsf_xml_out_end_element (xml);
	}
	if (gnm_style_is_element_set (style, MSTYLE_FONT_ITALIC)) {
		gsf_xml_out_start_element (xml, "i");
		xlsx_add_bool (xml, "val", gnm_style_get_font_italic (style));
		gsf_xml_out_end_element (xml);
	}
	if (gnm_style_is_element_set (style, MSTYLE_FONT_UNDERLINE)) {
		static const char * const types[] = {
			"none",
			"single",
			"double",
			"singleAccounting",
			"doubleAccounting"
		};
		unsigned uline = gnm_style_get_font_uline (style);

		if (uline < G_N_ELEMENTS (types)) {
			gsf_xml_out_start_element (xml, "u");
			gsf_xml_out_add_cstr
				(xml, "val", types[gnm_style_get_font_uline (style)]);
			gsf_xml_out_end_element (xml);
		}
	}
	if (gnm_style_is_element_set (style, MSTYLE_FONT_COLOR))
		xlsx_write_color_element
			(xml, "color",
			 gnm_style_get_font_color (style)->go_color);
	if (gnm_style_is_element_set (style, MSTYLE_FONT_NAME)) {
		gsf_xml_out_start_element (xml, "name");
		gsf_xml_out_add_cstr
			(xml, "val", gnm_style_get_font_name (style));
		gsf_xml_out_end_element (xml);
	}
	if (gnm_style_is_element_set (style, MSTYLE_FONT_SCRIPT)) {
		GOFontScript scr = gnm_style_get_font_script (style);
		const char *val;

		switch (scr) {
		case GO_FONT_SCRIPT_SUB: val = "subscript"; break;
		case GO_FONT_SCRIPT_SUPER: val = "superscript"; break;
		default:
		case GO_FONT_SCRIPT_STANDARD: val = "baseline"; break;
		}
		gsf_xml_out_start_element (xml, "vertAlign");
		gsf_xml_out_add_cstr (xml, "val", val);
		gsf_xml_out_end_element (xml);
	}
	if (gnm_style_is_element_set (style, MSTYLE_FONT_SIZE)) {
		gsf_xml_out_start_element (xml, "sz");
		go_xml_out_add_double
			(xml, "val", gnm_style_get_font_size (style));
		gsf_xml_out_end_element (xml);

	}
	if (gnm_style_is_element_set (style, MSTYLE_FONT_STRIKETHROUGH)) {
		gsf_xml_out_start_element (xml, "strike");
		xlsx_add_bool (xml, "val", gnm_style_get_font_strike (style));
		gsf_xml_out_end_element (xml);
	}
	gsf_xml_out_end_element (xml); /* font */
}

static GHashTable *
xlsx_write_fonts (XLSXWriteState *state, GsfXMLOut *xml)
{
	GHashTable *fonts_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
	GPtrArray  *styles_w_fonts = g_ptr_array_new ();
	unsigned int i;

	for (i = 0 ; i < state->styles_array->len ; i++) {
		GnmStyle const *style = g_ptr_array_index (state->styles_array, i);
		if (xlsx_has_font_style (style)) {
			gint font_n = xlsx_find_font (style, styles_w_fonts);
			if (font_n < 0) {
				g_ptr_array_add (styles_w_fonts, (gpointer)style);
				g_hash_table_insert (fonts_hash, (gpointer)style,
						     GINT_TO_POINTER (styles_w_fonts->len));
			} else
				g_hash_table_insert (fonts_hash, (gpointer)style,
						     GINT_TO_POINTER (font_n + 1));
		}
	}

	if (styles_w_fonts->len > 0) {
		gsf_xml_out_start_element (xml, "fonts");
		gsf_xml_out_add_int (xml, "count", styles_w_fonts->len);
		for (i = 0 ; i < styles_w_fonts->len ; i++) {
			GnmStyle const *style = g_ptr_array_index (styles_w_fonts, i);
			xlsx_write_font (state, xml, style);
		}
		gsf_xml_out_end_element (xml);
	}

	g_ptr_array_free (styles_w_fonts, TRUE);
	return fonts_hash;
}

static gint
xlsx_find_fill (GnmStyle const *style, GPtrArray *styles)
{
	unsigned i;
	int j;

	j = xlsx_find_predefined_fill (style);
	if (j >= 0)
		return j;
	for (i = 0 ; i < styles->len ; i++) {
		GnmStyle const *old_style = g_ptr_array_index (styles, i);
		if (style == old_style ||
		    (gnm_style_equal_elem (style, old_style, MSTYLE_COLOR_BACK) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_COLOR_PATTERN) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_PATTERN)))
			return (gint) i + N_PREDEFINED_FILLS;
	}
	return -1;
}

static gboolean
xlsx_has_background_style (GnmStyle const *style)
{
	return (gnm_style_is_element_set (style, MSTYLE_COLOR_BACK) ||
		gnm_style_is_element_set (style, MSTYLE_COLOR_PATTERN) ||
		gnm_style_is_element_set (style, MSTYLE_PATTERN));
}

static void
xlsx_write_background (XLSXWriteState *state, GsfXMLOut *xml,
		       GnmStyle const *style, gboolean invert_solid)
{
	/*
	 * MAGIC:
	 * Looks like pattern background and forground colours are inverted
	 * for dxfs with solid fills for no apparent reason.
	 */
	gboolean invert = FALSE;
	GnmColor *fg;
	GnmColor *bg;

	gsf_xml_out_start_element (xml, "fill");
	gsf_xml_out_start_element (xml, "patternFill");
	if (gnm_style_is_element_set (style, MSTYLE_PATTERN)) {
		gint pattern = gnm_style_get_pattern (style);
		const char *type;
		if (pattern <= 0 || pattern > (gint)G_N_ELEMENTS (pats)) {
			type = "none";
		} else {
			invert = (pattern == 1 && invert_solid);
			type = pats[pattern - 1];
		}
		gsf_xml_out_add_cstr_unchecked (xml, "patternType", type);
	}

	fg = gnm_style_is_element_set (style, MSTYLE_COLOR_BACK)
		? gnm_style_get_back_color (style)
		: NULL;
	bg = gnm_style_is_element_set (style, MSTYLE_COLOR_PATTERN)
		? gnm_style_get_pattern_color (style)
		: NULL;
	if (invert) {
		GnmColor *tmp = fg;
		fg = bg;
		bg = tmp;
	}

	if (fg)
		xlsx_write_color_element (xml, "fgColor", fg->go_color);
	if (bg)
		xlsx_write_color_element (xml, "bgColor", bg->go_color);

	gsf_xml_out_end_element (xml);
	gsf_xml_out_end_element (xml);
}

static GHashTable *
xlsx_write_fills (XLSXWriteState *state, GsfXMLOut *xml)
{
	GHashTable *fills_hash =  g_hash_table_new (g_direct_hash, g_direct_equal);
	GPtrArray  *styles_w_fills  = g_ptr_array_new ();
	unsigned i;

	for (i = 0 ; i < state->styles_array->len ; i++) {
		GnmStyle const *style = g_ptr_array_index (state->styles_array, i);
		if (xlsx_has_background_style (style)) {
			gint fill_n = xlsx_find_fill (style, styles_w_fills);
			if (fill_n < 0) {
				g_ptr_array_add (styles_w_fills, (gpointer)style);
				g_hash_table_insert
					(fills_hash, (gpointer)style,
					 GINT_TO_POINTER (styles_w_fills->len
							  + N_PREDEFINED_FILLS));
			} else
				g_hash_table_insert
					(fills_hash, (gpointer)style,
					 GINT_TO_POINTER
					 (fill_n + 1));
		}
	}

	gsf_xml_out_start_element (xml, "fills");
	gsf_xml_out_add_int (xml, "count", styles_w_fills->len
			     + N_PREDEFINED_FILLS);
	/* Excel considers the first few fills special (not according to */
	/* ECMA)                            */
	xlsx_write_predefined_fills (xml);
	for (i = 0 ; i < styles_w_fills->len ; i++) {
		GnmStyle const *style = g_ptr_array_index (styles_w_fills, i);
		xlsx_write_background (state, xml, style, FALSE);
	}
	gsf_xml_out_end_element (xml);

	g_ptr_array_free (styles_w_fills, TRUE);
	return fills_hash;
}

static gint
xlsx_find_border (GnmStyle const *style, GPtrArray  *styles)
{
	unsigned i;
	for (i = 0 ; i < styles->len ; i++) {
		GnmStyle const *old_style = g_ptr_array_index (styles, i);
		if (style == old_style ||
		    (gnm_style_equal_elem (style, old_style, MSTYLE_BORDER_TOP) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_BORDER_BOTTOM) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_BORDER_LEFT) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_BORDER_RIGHT) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_BORDER_DIAGONAL) &&
		     gnm_style_equal_elem (style, old_style, MSTYLE_BORDER_REV_DIAGONAL)))
			return (gint) i;
	}
	return -1;
}

static void
xlsx_write_border (XLSXWriteState *state, GsfXMLOut *xml, GnmBorder *border, GnmStyleElement elem)
{
	if (border == NULL)
		return;
	switch (elem) {
	case MSTYLE_BORDER_TOP:
		gsf_xml_out_start_element (xml, "top");
		break;
	case MSTYLE_BORDER_BOTTOM:
		gsf_xml_out_start_element (xml, "bottom");
		break;
	case MSTYLE_BORDER_LEFT:
		gsf_xml_out_start_element
			(xml, (state->version == ECMA_376_2006) ? "left" : "start");
		break;
	case MSTYLE_BORDER_DIAGONAL:
	case MSTYLE_BORDER_REV_DIAGONAL:
		gsf_xml_out_start_element (xml, "diagonal");
		break;
	default:
	case MSTYLE_BORDER_RIGHT:
		gsf_xml_out_start_element
			(xml, (state->version == ECMA_376_2006) ? "right" : "end");
		break;
	}
	switch (border->line_type) {
	case GNM_STYLE_BORDER_THIN:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "thin");
		break;
	case GNM_STYLE_BORDER_MEDIUM:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "medium");
		break;
	case GNM_STYLE_BORDER_DASHED:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "dashed");
		break;
	case GNM_STYLE_BORDER_DOTTED:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "dotted");
		break;
	case GNM_STYLE_BORDER_THICK:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "thick");
		break;
	case GNM_STYLE_BORDER_DOUBLE:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "double");
		break;
	case GNM_STYLE_BORDER_HAIR:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "hair");
		break;
	case GNM_STYLE_BORDER_MEDIUM_DASH:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "mediumDashed");
		break;
	case GNM_STYLE_BORDER_DASH_DOT:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "dashDot");
		break;
	case GNM_STYLE_BORDER_MEDIUM_DASH_DOT:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "mediumDashDot");
		break;
	case GNM_STYLE_BORDER_DASH_DOT_DOT:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "dashDotDot");
		break;
	case GNM_STYLE_BORDER_MEDIUM_DASH_DOT_DOT:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "mediumDashDotDot");
		break;
	case GNM_STYLE_BORDER_SLANTED_DASH_DOT:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "slantDashDot");
		break;
	default:
	case GNM_STYLE_BORDER_NONE:
		gsf_xml_out_add_cstr_unchecked (xml, "style", "none");
		break;
	}

	if (border->color != NULL)
		xlsx_write_color_element (xml, "color",  border->color->go_color);

	gsf_xml_out_end_element (xml);
}

static gboolean
xlsx_has_border_style (GnmStyle const *style)
{
	return (gnm_style_is_element_set (style, MSTYLE_BORDER_TOP) ||
		gnm_style_is_element_set (style, MSTYLE_BORDER_BOTTOM) ||
		gnm_style_is_element_set (style, MSTYLE_BORDER_LEFT) ||
		gnm_style_is_element_set (style, MSTYLE_BORDER_RIGHT) ||
		gnm_style_is_element_set (style, MSTYLE_BORDER_REV_DIAGONAL) ||
		gnm_style_is_element_set (style, MSTYLE_BORDER_DIAGONAL));
}

static void
xlsx_write_full_border (XLSXWriteState *state, GsfXMLOut *xml,
			GnmStyle const *style)
{
	gboolean diagonal_border_written = FALSE;
	gsf_xml_out_start_element (xml, "border");
	if (gnm_style_is_element_set (style, MSTYLE_BORDER_DIAGONAL)) {
		GnmBorder *border = gnm_style_get_border
			(style, MSTYLE_BORDER_DIAGONAL);
		gsf_xml_out_add_bool
			(xml, "diagonalUp",
			 (border && border->line_type != GNM_STYLE_BORDER_NONE));
	}
	if (gnm_style_is_element_set (style, MSTYLE_BORDER_REV_DIAGONAL)) {
		GnmBorder *border = gnm_style_get_border
			(style, MSTYLE_BORDER_REV_DIAGONAL);
		gsf_xml_out_add_bool
			(xml, "diagonalDown",
			 (border && border->line_type != GNM_STYLE_BORDER_NONE));
	}
	if (gnm_style_is_element_set (style, MSTYLE_BORDER_LEFT))
		xlsx_write_border (state, xml,
				   gnm_style_get_border
				   (style, MSTYLE_BORDER_LEFT),
				   MSTYLE_BORDER_LEFT);
	if (gnm_style_is_element_set (style, MSTYLE_BORDER_RIGHT))
		xlsx_write_border (state, xml,
				   gnm_style_get_border
				   (style, MSTYLE_BORDER_RIGHT),
				   MSTYLE_BORDER_RIGHT);
	if (gnm_style_is_element_set (style, MSTYLE_BORDER_TOP))
		xlsx_write_border (state, xml,
				   gnm_style_get_border
				   (style, MSTYLE_BORDER_TOP),
				   MSTYLE_BORDER_TOP);
	if (gnm_style_is_element_set (style, MSTYLE_BORDER_BOTTOM))
		xlsx_write_border (state, xml,
				   gnm_style_get_border
				   (style, MSTYLE_BORDER_BOTTOM),
				   MSTYLE_BORDER_BOTTOM);
	if (gnm_style_is_element_set (style, MSTYLE_BORDER_DIAGONAL)) {
		GnmBorder *border = gnm_style_get_border
			(style, MSTYLE_BORDER_DIAGONAL);
		if (border && border->line_type != GNM_STYLE_BORDER_NONE) {
			diagonal_border_written = TRUE;
			xlsx_write_border (state, xml,
					   border,
					   MSTYLE_BORDER_DIAGONAL);
		}
	}
	if (!diagonal_border_written &&
	    gnm_style_is_element_set (style, MSTYLE_BORDER_REV_DIAGONAL)) {
		GnmBorder *border = gnm_style_get_border
			(style, MSTYLE_BORDER_REV_DIAGONAL);
		if (border && border->line_type != GNM_STYLE_BORDER_NONE) {
			xlsx_write_border (state, xml,
					   border,
					   MSTYLE_BORDER_REV_DIAGONAL);
		}
	}
	gsf_xml_out_end_element (xml); /* border */
}


static GHashTable *
xlsx_write_borders (XLSXWriteState *state, GsfXMLOut *xml)
{
	GHashTable *border_hash =  g_hash_table_new (g_direct_hash, g_direct_equal);
	GPtrArray  *styles_w_border  = g_ptr_array_new ();
	unsigned int i;

	for (i = 0 ; i < state->styles_array->len ; i++) {
		GnmStyle const *style = g_ptr_array_index (state->styles_array, i);
		if (xlsx_has_border_style (style)) {
			gint border_n = xlsx_find_border (style, styles_w_border);
			if (border_n < 0) {
				g_ptr_array_add (styles_w_border, (gpointer)style);
				g_hash_table_insert (border_hash, (gpointer)style,
						     GINT_TO_POINTER (styles_w_border->len));
			} else
				g_hash_table_insert (border_hash, (gpointer)style,
						     GINT_TO_POINTER (border_n + 1));
		}
	}

	if (styles_w_border->len > 0) {
		gsf_xml_out_start_element (xml, "borders");
		gsf_xml_out_add_int (xml, "count", styles_w_border->len);
		for (i = 0; i < styles_w_border->len; i++) {
			GnmStyle const *style = g_ptr_array_index (styles_w_border, i);
			xlsx_write_full_border (state, xml, style);
		}
		gsf_xml_out_end_element (xml);
	}

	g_ptr_array_free (styles_w_border, TRUE);
	return border_hash;
}

static gboolean
xlsx_has_alignment_style (GnmStyle const *style)
{
	return gnm_style_is_element_set (style, MSTYLE_ALIGN_H)
		|| gnm_style_is_element_set (style, MSTYLE_ALIGN_V)
		|| gnm_style_is_element_set (style, MSTYLE_WRAP_TEXT)
		|| gnm_style_is_element_set (style, MSTYLE_SHRINK_TO_FIT)
		|| gnm_style_is_element_set (style, MSTYLE_ROTATION)
		|| gnm_style_is_element_set (style, MSTYLE_INDENT);
}

static gboolean
xlsx_has_protection_style (GnmStyle const *style)
{
	return gnm_style_is_element_set (style, MSTYLE_CONTENTS_LOCKED)
		|| gnm_style_is_element_set (style, MSTYLE_CONTENTS_HIDDEN);
}

static void
xlsx_write_style_write_alignment (G_GNUC_UNUSED XLSXWriteState *state, GsfXMLOut *xml,
		  GnmStyle const *style)
{
	gsf_xml_out_start_element (xml, "alignment");
	if (gnm_style_is_element_set (style, MSTYLE_ALIGN_H)) {
		switch (gnm_style_get_align_h (style)) {
		case GNM_HALIGN_LEFT:
			gsf_xml_out_add_cstr_unchecked (xml, "horizontal",
							"left");
			break;
		case GNM_HALIGN_RIGHT:
			gsf_xml_out_add_cstr_unchecked (xml, "horizontal",
							"right");
			break;
		case GNM_HALIGN_CENTER:
			gsf_xml_out_add_cstr_unchecked (xml, "horizontal",
							"center");
			break;
		case GNM_HALIGN_FILL:
			gsf_xml_out_add_cstr_unchecked (xml, "horizontal",
							"fill");
			break;
		case GNM_HALIGN_JUSTIFY:
			gsf_xml_out_add_cstr_unchecked (xml, "horizontal",
							"justify");
			break;
		case GNM_HALIGN_CENTER_ACROSS_SELECTION:
			gsf_xml_out_add_cstr_unchecked (xml, "horizontal",
							"centerContinuous");
			break;
		case GNM_HALIGN_DISTRIBUTED:
			gsf_xml_out_add_cstr_unchecked (xml, "horizontal",
							"distributed");
			break;
		case GNM_HALIGN_GENERAL:
		default:
			gsf_xml_out_add_cstr_unchecked (xml, "horizontal",
							"general");
			break;
		}
	}
	if (gnm_style_is_element_set (style, MSTYLE_ALIGN_V)) {
		switch (gnm_style_get_align_v (style)) {
		case GNM_VALIGN_TOP:
			gsf_xml_out_add_cstr_unchecked (xml, "vertical",
							"top");
			break;
		case GNM_VALIGN_BOTTOM:
			gsf_xml_out_add_cstr_unchecked (xml, "vertical",
							"bottom");
			break;
		case GNM_VALIGN_CENTER:
			gsf_xml_out_add_cstr_unchecked (xml, "vertical",
							"center");
			break;
		case GNM_VALIGN_JUSTIFY:
			gsf_xml_out_add_cstr_unchecked (xml, "vertical",
							"justify");
			break;
		default:
		case GNM_VALIGN_DISTRIBUTED:
			gsf_xml_out_add_cstr_unchecked (xml, "vertical",
							"distributed");
			break;
		}
	}
	if (gnm_style_is_element_set (style, MSTYLE_WRAP_TEXT)) {
		gsf_xml_out_add_bool (xml, "wrapText", gnm_style_get_wrap_text (style));
	}
	if (gnm_style_is_element_set (style, MSTYLE_SHRINK_TO_FIT)) {
		gsf_xml_out_add_bool (xml, "shrinkToFit", gnm_style_get_shrink_to_fit (style));
	}
	if (gnm_style_is_element_set (style, MSTYLE_ROTATION)) {
		int r = gnm_style_get_rotation (style);
		if (r == -1)
			r = 0xff;
		else if (r >= 270)
			r = (360 - r) + 90;
		gsf_xml_out_add_int (xml, "textRotation", r);
	}
	if (gnm_style_is_element_set (style, MSTYLE_INDENT)) {
		gsf_xml_out_add_int (xml, "indent", gnm_style_get_indent (style));
	}
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_style_write_protection (G_GNUC_UNUSED XLSXWriteState *state, GsfXMLOut *xml,
		  GnmStyle const *style)
{
	gsf_xml_out_start_element (xml, "protection");
	if (gnm_style_is_element_set (style, MSTYLE_CONTENTS_LOCKED))
		gsf_xml_out_add_bool (xml, "locked", gnm_style_get_contents_locked (style));
	if (gnm_style_is_element_set (style, MSTYLE_CONTENTS_HIDDEN))
		gsf_xml_out_add_bool (xml, "hidden", gnm_style_get_contents_hidden (style));
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_style (XLSXWriteState *state, GsfXMLOut *xml,
		  GnmStyle const *style, GHashTable *fills_hash,
		  GHashTable *num_format_hash, GHashTable *fonts_hash,
		  GHashTable *border_hash, gint id)
{
	gboolean alignment = xlsx_has_alignment_style (style);
	gboolean protection = xlsx_has_protection_style (style);
	gpointer tmp_fill, tmp_font, tmp_border;
	gboolean fill = (NULL != (tmp_fill = g_hash_table_lookup (fills_hash, style)));
	gboolean font = (NULL != (tmp_font = g_hash_table_lookup (fonts_hash, style)));
	gboolean border = (NULL != (tmp_border = g_hash_table_lookup (border_hash, style)));
	gboolean num_fmt = gnm_style_is_element_set (style, MSTYLE_FORMAT);

	if (id >= 0) {
		xlsx_add_bool (xml, "applyAlignment", alignment);
		xlsx_add_bool (xml, "applyBorder", border);
		xlsx_add_bool (xml, "applyFont", font);
		xlsx_add_bool (xml, "applyFill", fill);
		xlsx_add_bool (xml, "applyNumberFormat", num_fmt);
	}
	if (font)
		gsf_xml_out_add_int (xml, "fontId", GPOINTER_TO_INT (tmp_font) - 1);
	if (fill)
		gsf_xml_out_add_int (xml, "fillId", GPOINTER_TO_INT (tmp_fill) - 1);
	if (border)
		gsf_xml_out_add_int (xml, "borderId", GPOINTER_TO_INT (tmp_border) - 1);
	if (num_fmt)
		gsf_xml_out_add_int
			(xml, "numFmtId",
			 GPOINTER_TO_INT (g_hash_table_lookup (num_format_hash, style)));
	if (id >= 0)
		gsf_xml_out_add_int (xml, "xfId", id);

	if (alignment)
		xlsx_write_style_write_alignment (state, xml, style);
	if (protection)
		xlsx_write_style_write_protection (state, xml, style);
}

static void
xlsx_write_cellStyleXfs (XLSXWriteState *state, GsfXMLOut *xml,
		    GHashTable *fills_hash, GHashTable *num_format_hash,
		    GHashTable *fonts_hash, GHashTable *border_hash)
{
	gsf_xml_out_start_element (xml, "cellStyleXfs");
	gsf_xml_out_add_int (xml, "count", 1);
	gsf_xml_out_start_element (xml, "xf");
	xlsx_write_style
		(state, xml,
		 g_ptr_array_index (state->styles_array, 0),
		 fills_hash, num_format_hash, fonts_hash,
		 border_hash, -1);
	gsf_xml_out_end_element (xml);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_cellXfs (XLSXWriteState *state, GsfXMLOut *xml,
		    GHashTable *fills_hash, GHashTable *num_format_hash,
		    GHashTable *fonts_hash, GHashTable *border_hash)
{
	unsigned i, N = state->styles_array->len;

	if (N == 0)
		return;

	gsf_xml_out_start_element (xml, "cellXfs");
	gsf_xml_out_add_int (xml, "count", N);
	for (i = 0; i < N; i++) {
		GnmStyle const *style =
			g_ptr_array_index (state->styles_array, i);
		gsf_xml_out_start_element (xml, "xf");
		xlsx_write_style
			(state, xml, style,
			 fills_hash, num_format_hash, fonts_hash,
			 border_hash, 0);
		gsf_xml_out_end_element (xml);
	}
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_dxfs (XLSXWriteState *state, GsfXMLOut *xml,
		 GHashTable *num_format_hash)
{
	unsigned i, N = state->dxfs_array->len;
	int id = NUM_FORMAT_BASE + g_hash_table_size (num_format_hash);

	if (N == 0)
		return;

	gsf_xml_out_start_element (xml, "dxfs");
	gsf_xml_out_add_int (xml, "count", N);
	for (i = 0; i < N; i++) {
		GnmStyle *style =
			g_ptr_array_index (state->dxfs_array, i);
		gsf_xml_out_start_element (xml, "dxf");
		if (xlsx_has_font_style (style))
			xlsx_write_font (state, xml, style);
		if (xlsx_has_alignment_style (style))
			xlsx_write_style_write_alignment (state, xml, style);
		if (xlsx_has_background_style (style))
			xlsx_write_background (state, xml, style, TRUE);
		if (gnm_style_is_element_set (style, MSTYLE_FORMAT)) {
			GOFormat const *format = gnm_style_get_format (style);
			xlsx_write_num_format (state, xml, format, id++);
		}
		if (xlsx_has_border_style (style))
			xlsx_write_full_border (state, xml, style);

		gsf_xml_out_end_element (xml);
	}
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_styles (XLSXWriteState *state, GsfOutfile *wb_part)
{
	GsfOutput *part = gsf_outfile_open_pkg_add_rel (state->xl_dir, "styles.xml",
		"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml",
		wb_part,
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles");
	GsfXMLOut *xml = gsf_xml_out_new (part);
	GHashTable *fills_hash, *num_format_hash, *fonts_hash, *border_hash;

	gsf_xml_out_start_element (xml, "styleSheet");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	/* Note the schema does not allow the attribute xml:space */

	/* The order of elements is fixed in the schema (xsd:sequence) */
	num_format_hash = xlsx_write_num_formats (state, xml);
	fonts_hash = xlsx_write_fonts (state, xml);
	fills_hash = xlsx_write_fills (state, xml);
	border_hash = xlsx_write_borders (state, xml);
	xlsx_write_cellStyleXfs (state, xml, fills_hash, num_format_hash, fonts_hash, border_hash);
	xlsx_write_cellXfs (state, xml, fills_hash, num_format_hash, fonts_hash, border_hash);
	/* <xsd:element name="cellStyles" type="CT_CellStyles" minOccurs="0" maxOccurs="1"/> */
	xlsx_write_dxfs (state, xml, num_format_hash);
	/* <xsd:element name="tableStyles" type="CT_TableStyles" minOccurs="0" maxOccurs="1"/> */
	/* <xsd:element name="colors" type="CT_Colors" minOccurs="0" maxOccurs="1"/> */
	/* <xsd:element name="extLst" type="CT_ExtensionList" minOccurs="0" maxOccurs="1"/> */

	g_hash_table_destroy (fills_hash);
	g_hash_table_destroy (fonts_hash);
	g_hash_table_destroy (num_format_hash);
	g_hash_table_destroy (border_hash);
	gsf_xml_out_end_element (xml); /* </styleSheet> */
	g_object_unref (xml);
	gsf_output_close (part);
	g_object_unref (part);
}

#define XLSX_MAX_COLS	gnm_sheet_get_max_cols (state->sheet)	/* default is (2^14) */
#define XLSX_MAX_ROWS	gnm_sheet_get_max_rows (state->sheet)	/* default is (2^20) */

static void
xlsx_write_sheet_view (GsfXMLOut *xml, SheetView const *sv)
{
	Sheet const *sheet = sv_sheet (sv);
	GnmColor *sheet_auto   = sheet_style_get_auto_pattern_color (sheet);
	GnmColor *default_auto = style_color_auto_pattern ();
	GnmCellPos topLeft, frozen_topLeft;
	char const *activePane = NULL;
	int const frozen_height = sv->unfrozen_top_left.row -
		sv->frozen_top_left.row;
	int const frozen_width = sv->unfrozen_top_left.col -
		sv->frozen_top_left.col;
	int tmp;

	if (frozen_width > 0) {
		topLeft.col		= sv->frozen_top_left.col;
		frozen_topLeft.col	= sv->initial_top_left.col;
	} else {
		topLeft.col		= sv->initial_top_left.col;
		frozen_topLeft.col	= sv->frozen_top_left.col;
	}
	if (frozen_height > 0) {
		topLeft.row		= sv->frozen_top_left.row;
		frozen_topLeft.row	= sv->initial_top_left.row;
	} else {
		topLeft.row		= sv->initial_top_left.row;
		frozen_topLeft.row	= sv->frozen_top_left.row;
	}

	gsf_xml_out_start_element (xml, "sheetView");
	if (topLeft.col > 0 || topLeft.row > 0) /* A1 is the default */
		xlsx_add_pos (xml, "topLeftCell", &topLeft);
	gsf_xml_out_add_int (xml, "workbookViewId",
		wb_view_get_index_in_wb (sv_wbv (sv)));

	tmp = (int) (100.* sheet->last_zoom_factor_used + .5);
	if (tmp != 100)
		gsf_xml_out_add_int (xml, "zoomScale", tmp);

	switch (sv->view_mode) {
	case GNM_SHEET_VIEW_NORMAL_MODE : break;
	case GNM_SHEET_VIEW_PAGE_BREAK_MODE :
		gsf_xml_out_add_cstr_unchecked (xml, "view", "pageBreakPreview"); break;
	case GNM_SHEET_VIEW_LAYOUT_MODE :
		gsf_xml_out_add_cstr_unchecked (xml, "view", "pageLayout"); break;
	}

	if (sheet->hide_grid)
		gsf_xml_out_add_cstr_unchecked (xml, "showGridLines", "0");
	if (sheet->display_formulas)
		gsf_xml_out_add_cstr_unchecked (xml, "showFormulas", "1");
	if (sheet->hide_col_header || sheet->hide_row_header)
		gsf_xml_out_add_cstr_unchecked (xml, "showRowColHeaders", "0");
	if (sheet->hide_zero)
		gsf_xml_out_add_cstr_unchecked (xml, "showZeros", "0");
	if (!sheet->display_outlines)
		gsf_xml_out_add_cstr_unchecked (xml, "showOutlineSymbols", "0");
	if (sheet->text_is_rtl)
		gsf_xml_out_add_cstr_unchecked (xml, "rightToLeft", "1");
	if (sheet == wb_view_cur_sheet (sv_wbv (sv)))
		gsf_xml_out_add_cstr_unchecked (xml, "tabSelected", "1");

	if (!style_color_equal (sheet_auto, default_auto)) {
		gsf_xml_out_add_cstr_unchecked (xml, "defaultGridColor", "1");
#if 0
		gsf_xml_out_add_int (xml, "colorId", grid_color_index);
#endif
	}
	style_color_unref (sheet_auto);
	style_color_unref (default_auto);

	if (gnm_sheet_view_is_frozen (sv)) {
		activePane = "bottomRight"; /* h&v freeze */

		gsf_xml_out_start_element (xml, "pane");
		if (frozen_width > 0)
			gsf_xml_out_add_int (xml, "xSplit", frozen_width);
		else
			activePane = "bottomLeft"; /* v freeze */
		if (frozen_height > 0)
			gsf_xml_out_add_int (xml, "ySplit", frozen_height);
		else
			activePane = "topRight"; /* h freeze */
		xlsx_add_pos (xml, "topLeftCell", &frozen_topLeft);
		gsf_xml_out_add_cstr_unchecked (xml, "activePane", activePane);
		gsf_xml_out_add_cstr_unchecked (xml, "state", "frozen");
		gsf_xml_out_end_element (xml); /* </pane> */
	}

	gsf_xml_out_start_element (xml, "selection");
	if (NULL != activePane)
		gsf_xml_out_add_cstr_unchecked (xml, "pane", activePane);
	/* activeCellId is always 0 for gnumeric */
	xlsx_add_pos (xml, "activeCell", &sv->edit_pos);
	xlsx_add_range_list (xml, "sqref", sv->selections);
	gsf_xml_out_end_element (xml); /* </selection> */

	gsf_xml_out_end_element (xml); /* </sheetView> */
}

static void
xlsx_write_init_row (gboolean *needs_row, GsfXMLOut *xml, int r, char const *span)
{
	if (*needs_row) {
		gsf_xml_out_start_element (xml, "row");
		gsf_xml_out_add_int (xml, "r", r+1);
		gsf_xml_out_add_cstr_unchecked (xml, "spans", span);
		*needs_row = FALSE;
	}
}

static gint
xlsx_get_style_id (XLSXWriteState *state, GnmStyle const *style)
{
	gpointer tmp;

	g_return_val_if_fail (style != NULL, 0);

	if (NULL == (tmp = g_hash_table_lookup (state->styles_hash, style))) {
		g_ptr_array_add (state->styles_array, (gpointer) style);
		tmp = GINT_TO_POINTER (state->styles_array->len);
		gnm_style_ref (style);
		g_hash_table_insert (state->styles_hash, (gpointer) style, tmp);
	}
	return GPOINTER_TO_INT (tmp) - 1;
}

static gint
xlsx_get_cond_style_id (XLSXWriteState *state, GnmStyle const *style)
{
	gpointer tmp;

	g_return_val_if_fail (style != NULL, 0);

	if (NULL == (tmp = g_hash_table_lookup (state->dxfs_hash, style))) {
		g_ptr_array_add (state->dxfs_array, (gpointer)style);
		tmp = GINT_TO_POINTER (state->dxfs_array->len);
		g_hash_table_insert (state->dxfs_hash, (gpointer)style, tmp);
	}
	return GPOINTER_TO_INT (tmp) - 1;
}

static gboolean
row_boring (Sheet *sheet, int r)
{
	ColRowInfo const *ri = sheet_row_get (sheet, r);
	if (!ri)
		return TRUE;

	return (!ri->hard_size &&
		fabs (ri->size_pts - sheet->rows.default_style.size_pts) < 1e-6 &&
		!ri->is_collapsed &&
		ri->visible &&
		ri->outline_level == 0);
}

static void
xlsx_write_cells (XLSXWriteState *state, GsfXMLOut *xml,
		  GnmRange const *extent, GPtrArray *col_styles)
{
	int r, c;
	char *content;
	GnmParsePos pp;
	GnmExprTop const *texpr;
	char *cheesy_span = g_strdup_printf ("%d:%d", extent->start.col+1, extent->end.col+1);
	Sheet *sheet = (Sheet *)state->sheet;
	GPtrArray *all_cells = sheet_cells (sheet, extent);
	guint cno = 0;
	int *boring_count;
	GByteArray *non_defaults_rows = sheet_style_get_nondefault_rows (sheet, col_styles);

	boring_count = g_new0 (int, extent->end.row + 1);
	r = extent->end.row;
	boring_count[r] = row_boring (sheet, r);
	while (r-- > extent->start.row)
		boring_count[r] = row_boring (sheet, r)
			? 1 + boring_count[r + 1]
			: 0;

	/* Add a NULL to simplify code.  */
	g_ptr_array_add (all_cells, NULL);

	gsf_xml_out_start_element (xml, "sheetData");
       	for (r = extent->start.row ; r <= extent->end.row ; r++) {
		gboolean needs_row = TRUE;

		if (boring_count[r] == 0) {
			ColRowInfo const *ri = sheet_row_get (sheet, r);

			/* The code here needs to match row_boring.  */

			if (ri->hard_size) {
				xlsx_write_init_row (&needs_row, xml, r, cheesy_span);
				gsf_xml_out_add_cstr_unchecked (xml, "customHeight", "1");
			}
			if (ri->hard_size || fabs (ri->size_pts - sheet->cols.default_style.size_pts) > 1e-6) {
				xlsx_write_init_row (&needs_row, xml, r, cheesy_span);
				go_xml_out_add_double (xml, "ht", ri->size_pts);
			}
			if (ri->is_collapsed) {
				xlsx_write_init_row (&needs_row, xml, r, cheesy_span);
				gsf_xml_out_add_cstr_unchecked (xml, "collapsed", "1");
			}
			if (!ri->visible) {
				xlsx_write_init_row (&needs_row, xml, r, cheesy_span);
				gsf_xml_out_add_cstr_unchecked (xml, "hidden", "1");
			}
			if (ri->outline_level > 0) {
				xlsx_write_init_row (&needs_row, xml, r, cheesy_span);
				gsf_xml_out_add_int (xml, "outlineLevel", ri->outline_level);
			}
		}

		/*
		 * If we didn't have to write anything yet and if the whole
		 * row -- and possibly the ones after it -- are all
		 * using default style, skip them.
		 */
		if (needs_row) {
			GnmCell *cell = g_ptr_array_index (all_cells, cno);
			int dr, rows = (cell ? cell->pos.row : extent->end.row + 1) - r;
			rows = MIN (rows, boring_count[r]);
			for (dr = 0; dr < rows; dr++)
				if (non_defaults_rows->data[r + dr])
					break;
			rows = MIN (rows, dr);
			if (rows > 0) {
				r += (rows - 1);
				continue;
			}
		}

		for (c = extent->start.col ; c <= extent->end.col ; c++) {
			GnmCell *cell;
			GnmValue const *val;
			GnmStyle const *style;
			GnmStyle *style1 = NULL;
			gint style_id;
			GOFormat const *fmt1, *fmt2;

			cell = g_ptr_array_index (all_cells, cno);
			if (cell && cell->pos.row == r && cell->pos.col == c) {
				cno++;
				val = cell->value;
			} else {
				cell = NULL;
				val = NULL;
			}

			/* FIXME: Use sheet_style_get_row once per row */
			style = sheet_style_get (sheet, c, r);
			fmt1 = gnm_style_get_format (style);
			fmt2 = cell ? gnm_cell_get_format_given_style (cell, style) : fmt1;
			if (fmt1 != fmt2 && !go_format_is_markup (fmt2)) {
				style = style1 = gnm_style_dup (style);
				gnm_style_set_format (style1, fmt2);
			}
			style_id = style && style != g_ptr_array_index (col_styles, c)
				? xlsx_get_style_id (state, style)
				: -1;
			if (style1)
				gnm_style_unref (style1);

			if (cell) {
				char const *type;
				gboolean inlineStr = FALSE;
				int str_id = -1;

				xlsx_write_init_row (&needs_row, xml, r, cheesy_span);
				gsf_xml_out_start_element (xml, "c");
				gsf_xml_out_add_cstr_unchecked (xml, "r",
					cell_coord_name (c, r));
				if (style_id > -1)
					gsf_xml_out_add_int (xml, "s", style_id);

				switch (val->v_any.type) {
				default :
				case VALUE_EMPTY:
					type = NULL; /* FIXME : what to do ? */
					break;
				case VALUE_BOOLEAN:
					type = "b";
					break;
				case VALUE_FLOAT:
					type = ""; /* "n" is the default */
					break;
				case VALUE_ERROR:
					type = "e";
					break;
				case VALUE_STRING:
					/* A reasonable approximation of * 'is_shared'.  It can get spoofed by
					 * rich text references to a base * string */
					if (go_string_get_ref_count (val->v_str.val) > 1) {
						str_id = xlsx_shared_string (state, val);
						type = "s";
					} else if (gnm_cell_has_expr (cell))
						type = "str";
					else {
						type = "inlineStr";
						inlineStr = TRUE;
					}
					break;
				case VALUE_CELLRANGE:
				case VALUE_ARRAY:
					type = NULL; /* FIXME */
					break;
				}

				if (NULL != type && *type)
					gsf_xml_out_add_cstr_unchecked (xml, "t", type);

				if (gnm_cell_has_expr (cell)) {
					texpr = cell->base.texpr;
					if (!gnm_expr_top_is_array_elem (texpr, NULL, NULL)) {
						gsf_xml_out_start_element (xml, "f");

						if (gnm_expr_top_is_array_corner (texpr)) {
							GnmRange r;
							int cols, rows;

							gnm_expr_top_get_array_size (texpr, &cols, &rows);
							range_init_cellpos_size (&r, &cell->pos,
								cols, rows);
							gsf_xml_out_add_cstr_unchecked (xml, "t", "array");
							xlsx_add_range (xml, "ref", &r);
						}
						content = gnm_expr_top_as_string (cell->base.texpr,
							parse_pos_init_cell (&pp, cell), state->convs);
						gsf_xml_out_add_cstr (xml, NULL, content);
						g_free (content);

						gsf_xml_out_end_element (xml); /* </f> */
					}
				}

				if (inlineStr) {
					GOFormat const *fmt = VALUE_FMT (val);
					PangoAttrList *attrs =
						fmt && go_format_is_markup (fmt)
						? (PangoAttrList *)go_format_get_markup (VALUE_FMT (val))
						: NULL;

					gsf_xml_out_start_element (xml, "is");
					xlsx_write_rich_text (xml,
							      value_peek_string (val),
							      attrs,
							      FALSE);
					gsf_xml_out_end_element (xml); /* </is> */
				} else if (type) {
					gsf_xml_out_start_element (xml, "v");
					if (str_id >= 0)
						gsf_xml_out_add_int (xml, NULL, str_id);
					else if (VALUE_IS_BOOLEAN (val))
						xlsx_add_bool (xml, NULL, value_get_as_int (val));
					else {
						GString *str = g_string_new (NULL);
						value_get_as_gstring (cell->value, str, state->convs);
						gsf_xml_out_add_cstr (xml, NULL, str->str);
						g_string_free (str, TRUE);
					}
					gsf_xml_out_end_element (xml); /* </v> */
				}

				gsf_xml_out_end_element (xml); /* </c> */
			} else if (style_id > -1) {
				xlsx_write_init_row (&needs_row, xml, r, cheesy_span);
				gsf_xml_out_start_element (xml, "c");
				gsf_xml_out_add_cstr_unchecked (xml, "r",
								cell_coord_name (c, r));
				gsf_xml_out_add_int (xml, "s", style_id);
				gsf_xml_out_end_element (xml); /* </c> */
			}
		}
		if (!needs_row)
			gsf_xml_out_end_element (xml); /* </row> */
	}
	gsf_xml_out_end_element (xml); /* </sheetData> */
	g_byte_array_free (non_defaults_rows, TRUE);
	g_free (boring_count);
	g_ptr_array_free (all_cells, TRUE);
	g_free (cheesy_span);
}

static void
xlsx_write_merges (XLSXWriteState *state, GsfXMLOut *xml)
{
	GSList *ptr;

	if (NULL != (ptr = state->sheet->list_merged)) {
		gsf_xml_out_start_element (xml, "mergeCells");
		for (; ptr != NULL ; ptr = ptr->next) {
			gsf_xml_out_start_element (xml, "mergeCell");
			xlsx_add_range (xml, "ref", ptr->data);
			gsf_xml_out_end_element (xml); /* </mergeCell> */
		}
		gsf_xml_out_end_element (xml); /* </mergeCells> */
	}
}

static void
xlsx_write_cond_rule (XLSXWriteState *state, GsfXMLOut *xml,
		      GnmStyleCond *cond, GnmParsePos const *pp)
{
	int i, n;
	const char *type = NULL;
	const char *operator = NULL;
	GnmExprTop const *alt_texpr;
	gboolean needs_alt_texpr = FALSE;

	gsf_xml_out_start_element (xml, "cfRule");
	switch (cond->op) {
	case GNM_STYLE_COND_BETWEEN:
		n = 2; operator = "between";
		break;
	case GNM_STYLE_COND_NOT_BETWEEN:
		n = 2; operator = "notBetween";
		break;
	case GNM_STYLE_COND_EQUAL:
		n = 1; operator = "equal";
		break;
	case GNM_STYLE_COND_NOT_EQUAL:
		n = 1; operator = "notEqual";
		break;
	case GNM_STYLE_COND_GT:
		n = 1; operator = "greaterThan";
		break;
	case GNM_STYLE_COND_LT:
		n = 1; operator = "lessThan";
		break;
	case GNM_STYLE_COND_GTE:
		n = 1; operator = "greaterThanOrEqual";
		break;
	case GNM_STYLE_COND_LTE:
		n = 1; operator = "lessThanOrEqual";
		break;
	case GNM_STYLE_COND_CONTAINS_BLANKS:
		needs_alt_texpr = TRUE;
		n = 1; type = "containsBlanks";
		break;
	case GNM_STYLE_COND_NOT_CONTAINS_BLANKS:
		needs_alt_texpr = TRUE;
		n = 1; type = "notContainsBlanks";
		break;
	case GNM_STYLE_COND_CONTAINS_ERR:
		needs_alt_texpr = TRUE;
		n = 1; type = "containsErrors";
		break;
	case GNM_STYLE_COND_NOT_CONTAINS_ERR:
		needs_alt_texpr = TRUE;
		n = 1; type = "notContainsErrors";
		break;
	case GNM_STYLE_COND_CUSTOM:
		n = 1; type = "expression";
		break;
	case GNM_STYLE_COND_CONTAINS_STR:
		needs_alt_texpr = TRUE;
		n = 1; type = "containsText";
		break;
	case GNM_STYLE_COND_NOT_CONTAINS_STR:
		needs_alt_texpr = TRUE;
		n = 1; type = "notContainsText";
		break;
	case GNM_STYLE_COND_BEGINS_WITH_STR:
		needs_alt_texpr = TRUE;
		n = 1; type = "beginsWith";
		break;
	case GNM_STYLE_COND_ENDS_WITH_STR:
		needs_alt_texpr = TRUE;
		n = 1; type = "endsWith";
		break;

	case GNM_STYLE_COND_NOT_BEGINS_WITH_STR:
	case GNM_STYLE_COND_NOT_ENDS_WITH_STR:
		needs_alt_texpr = TRUE;
		n = 1; type = "expression";
		break;

	default:
		g_assert_not_reached ();
	}

	alt_texpr = needs_alt_texpr
		? gnm_style_cond_get_alternate_expr (cond)
		: NULL;

	gsf_xml_out_add_cstr_unchecked (xml, "type", type ? type : "cellIs");
	gsf_xml_out_add_int (xml, "dxfId",
			     xlsx_get_cond_style_id (state, cond->overlay));
	gsf_xml_out_add_int (xml, "priority", 1) ;
	gsf_xml_out_add_int (xml, "stopIfTrue", 1) ;
	if (operator)
		gsf_xml_out_add_cstr_unchecked (xml, "operator", operator);
	for (i = 0; i < n; i++) {
		GnmExprTop const *texpr = alt_texpr
			? alt_texpr
			: gnm_style_cond_get_expr (cond, i);
		char *str = gnm_expr_top_as_string (texpr, pp, state->convs);
		gsf_xml_out_simple_element (xml, "formula", str);
		g_free (str);
	}
	if (alt_texpr)
		gnm_expr_top_unref (alt_texpr);
	gsf_xml_out_end_element (xml); /* </cfRule> */
}


static void
xlsx_write_conditional_formatting (XLSXWriteState *state, GsfXMLOut *xml)
{
	GnmStyleList *cond_styles =
		sheet_style_collect_conditions (state->sheet, NULL);
	GnmStyleList *l;
	if (!cond_styles)
		return;

	for (l = cond_styles; l; l = l->next) {
		GnmStyleRegion const *sr = l->data;
		GnmStyleConditions *sc = gnm_style_get_conditions (sr->style);
		GPtrArray const *conds = gnm_style_conditions_details (sc);
		unsigned ui;
		GnmParsePos pp;

		if (!conds)
			continue;

		parse_pos_init (&pp, NULL, state->sheet,
				sr->range.start.col, sr->range.start.row);
		gsf_xml_out_start_element (xml, "conditionalFormatting");
		xlsx_add_range (xml, "sqref", &sr->range);
		for (ui = 0; ui < conds->len; ui++) {
			GnmStyleCond *cond = g_ptr_array_index (conds, ui);
			xlsx_write_cond_rule (state, xml, cond, &pp);
		}
		gsf_xml_out_end_element (xml); /* </conditionalFormatting> */
	}

	style_list_free (cond_styles);
}


static void
xlsx_write_validation_expr (XLSXClosure *info, GnmCellPos const *pos,
			    char const *elem, GnmExprTop const *texpr)
{
	if (NULL != texpr) {
		GnmParsePos pp;
		char *str = gnm_expr_top_as_string (texpr,
			parse_pos_init (&pp, NULL, (Sheet *)info->state->sheet,
					pos->col, pos->row),
			info->state->convs);
		gsf_xml_out_simple_element (info->xml, elem, str);
		g_free (str);
	}
}

static void
xlsx_write_validation (XLValInputPair const *vip, G_GNUC_UNUSED gpointer dummy, XLSXClosure *info)
{
#if 0
	/* Get docs on this */
	"imeMode" default="noControl"
		"noControl"
		"off"
		"on"
		"disabled"
		"hiragana"
		"fullKatakana"
		"halfKatakana"
		"fullAlpha"
		"halfAlpha"
		"fullHangul"
		"halfHangul"
#endif
	char const *tmp;

	gsf_xml_out_start_element (info->xml, "dataValidation");

	if (NULL != vip->v) {
		tmp = NULL;
		switch (vip->v->type) {
		default : /* fall back to the default */
		case GNM_VALIDATION_TYPE_ANY : /* the default "none" */  break;
		case GNM_VALIDATION_TYPE_AS_INT :		tmp = "whole"; break;
		case GNM_VALIDATION_TYPE_AS_NUMBER :	tmp = "decimal"; break;
		case GNM_VALIDATION_TYPE_IN_LIST :		tmp = "list"; break;
		case GNM_VALIDATION_TYPE_AS_DATE :		tmp = "date"; break;
		case GNM_VALIDATION_TYPE_AS_TIME :		tmp = "time"; break;
		case GNM_VALIDATION_TYPE_TEXT_LENGTH :	tmp = "textLength"; break;
		case GNM_VALIDATION_TYPE_CUSTOM :		tmp = "custom"; break;
		}
		if (NULL != tmp)
			gsf_xml_out_add_cstr_unchecked (info->xml, "type", tmp);

		tmp = NULL;
		switch (vip->v->op) {
		default : /* fall back to the default */
		case GNM_VALIDATION_OP_BETWEEN :	/* the default "between" */ break;
		case GNM_VALIDATION_OP_NOT_BETWEEN: tmp = "notBetween"; break;
		case GNM_VALIDATION_OP_EQUAL :	tmp = "equal"; break;
		case GNM_VALIDATION_OP_NOT_EQUAL :	tmp = "notEqual"; break;
		case GNM_VALIDATION_OP_LT :		tmp = "lessThan"; break;
		case GNM_VALIDATION_OP_GT :		tmp = "greaterThan"; break;
		case GNM_VALIDATION_OP_LTE :	tmp = "lessThanOrEqual"; break;
		case GNM_VALIDATION_OP_GTE :	tmp = "greaterThanOrEqual"; break;
		}
		if (NULL != tmp)
			gsf_xml_out_add_cstr_unchecked (info->xml, "operator", tmp);

		tmp = NULL;
		switch (vip->v->style) {
		default : /* fall back to the default */
		case GNM_VALIDATION_STYLE_STOP : /* "stop" the default */ break;
		case GNM_VALIDATION_STYLE_WARNING : tmp = "warning"; break;
		case GNM_VALIDATION_STYLE_INFO : tmp = "information"; break;
		}
		if (NULL != tmp)
			gsf_xml_out_add_cstr_unchecked (info->xml, "errorStyle", tmp);

		if (vip->v->allow_blank)
			xlsx_add_bool (info->xml, "allowBlank", TRUE);

		// Inverted sense
		xlsx_add_bool (info->xml, "showDropDown", !vip->v->use_dropdown);

		if (NULL != vip->v->title)
			gsf_xml_out_add_cstr (info->xml, "errorTitle", vip->v->title->str);
		if (NULL != vip->v->msg)
			gsf_xml_out_add_cstr (info->xml, "error", vip->v->msg->str);
	}

	/* ?? Always TRUE but not the default ?? */
	xlsx_add_bool (info->xml, "showInputMessage", TRUE);
	xlsx_add_bool (info->xml, "showErrorMessage", TRUE);

	if (NULL != vip->msg) {
		char const *str;
		if (NULL != (str = gnm_input_msg_get_title (vip->msg)))
			gsf_xml_out_add_cstr (info->xml, "promptTitle", str);
		if (NULL != (str = gnm_input_msg_get_msg (vip->msg)))
			gsf_xml_out_add_cstr (info->xml, "prompt", str);
	}

	xlsx_add_range_list (info->xml, "sqref", vip->ranges);

	if (NULL != vip->v) {
		GnmRange const *first = vip->ranges->data;
		xlsx_write_validation_expr (info, &first->start,
			"formula1", vip->v->deps[0].base.texpr);
		xlsx_write_validation_expr (info, &first->start,
			"formula2", vip->v->deps[1].base.texpr);
	}

	gsf_xml_out_end_element (info->xml); /*  </dataValidation> */
}

static int
by_first_range (gpointer vip_a, G_GNUC_UNUSED gpointer val_a,
		gpointer vip_b, G_GNUC_UNUSED gpointer val_b,
		G_GNUC_UNUSED gpointer user)
{
	const XLValInputPair *a = vip_a;
	const XLValInputPair *b = vip_b;
	return gnm_range_compare (a->ranges->data, b->ranges->data);
}

static void
xlsx_write_validations (XLSXWriteState *state, GsfXMLOut *xml, G_GNUC_UNUSED GnmRange const *extent)
{
	GnmStyleList *validations = sheet_style_collect_validations (state->sheet, NULL);

	if (NULL != validations) {
		XLSXClosure info = { state, xml };
		/* filter on logical max, not extent.  XL allows validations
		 * past the stated dimension */
		GHashTable *group = xls_collect_validations (validations,
			XLSX_MAX_COLS, XLSX_MAX_ROWS);

		gsf_xml_out_start_element (xml, "dataValidations");
		gsf_xml_out_add_int (xml, "count", g_hash_table_size (group)) ;
		gnm_hash_table_foreach_ordered
			(group, (GHFunc)xlsx_write_validation,
			 by_first_range,
			 &info);
		gsf_xml_out_end_element (xml); /*  </dataValidations> */

		g_hash_table_destroy (group);
		style_list_free (validations);
	}
}

static void
xlsx_write_hlink (GnmHLink const *lnk, GSList *ranges, XLSXClosure *info)
{
	gchar *target = g_strdup (gnm_hlink_get_target (lnk));
	gchar const *rid = NULL;
	gchar *location = NULL;
	gchar const *tip = gnm_hlink_get_tip (lnk);
	GType const t = G_OBJECT_TYPE (lnk);

	if (target &&
	    (g_type_is_a (t, gnm_hlink_url_get_type ()) ||
	     GNM_IS_HLINK_EXTERNAL (lnk))) {
		// URLs, including email.

		char *hash = strchr (target, '#');
		if (hash) {
			location = g_strdup (hash + 1);
			*hash = 0;
		}

		rid = gsf_outfile_open_pkg_add_extern_rel (
			GSF_OUTFILE_OPEN_PKG (gsf_xml_out_get_output (info->xml)),
			target, ns_rel_hlink);
	} else if (t == gnm_hlink_cur_wb_get_type ()) {
		location = target;
		target = NULL;
	} else {
		g_free (target);
		g_free (location);
		return;
	}

	for (; ranges  != NULL ; ranges = ranges->next) {
		GnmRange const *range = ranges->data;

		gsf_xml_out_start_element (info->xml, "hyperlink");
		xlsx_add_range (info->xml, "ref", range);

		if (rid)
			gsf_xml_out_add_cstr (info->xml, "r:id", rid);
		if (location)
			gsf_xml_out_add_cstr (info->xml, "location", location);
		if (tip)
			gsf_xml_out_add_cstr (info->xml, "tooltip", tip);

		gsf_xml_out_end_element (info->xml); /*  </hyperlink> */
	}

	g_free (target);
	g_free (location);
}

static int
by_hlink_order (G_GNUC_UNUSED gpointer link_a, gpointer val_a,
		G_GNUC_UNUSED gpointer link_b, gpointer val_b,
		G_GNUC_UNUSED gpointer user)
{
	GList *ranges_a = val_a;
	GList *ranges_b = val_b;

	return gnm_range_compare (ranges_a->data, ranges_b->data);
}

static void
xlsx_write_hlinks (XLSXWriteState *state, GsfXMLOut *xml, G_GNUC_UNUSED GnmRange const *extent)
{
	GnmStyleList *hlinks = sheet_style_collect_hlinks (state->sheet, NULL);

	if (NULL != hlinks) {
		XLSXClosure info = { state, xml };
		/* filter on logical max, not extent.  XL allows validations
		 * past the stated dimension */
		GHashTable *group = xls_collect_hlinks (hlinks,
			XLSX_MAX_COLS, XLSX_MAX_ROWS);

		gsf_xml_out_start_element (xml, "hyperlinks");
		gnm_hash_table_foreach_ordered
			(group, (GHFunc) xlsx_write_hlink,
			 by_hlink_order,
			 &info);
		gsf_xml_out_end_element (xml); /*  </hyperlinks> */

		g_hash_table_destroy (group);
		style_list_free (hlinks);
	}
}

static void
xlsx_write_col (XLSXWriteState *state, GsfXMLOut *xml,
		ColRowInfo const *ci, int first, int last,
		GnmStyle *style)
{
	double const def_width = state->sheet->cols.default_style.size_pts;
	gint style_id = xlsx_get_style_id (state, style);

	gsf_xml_out_start_element (xml, "col");
	gsf_xml_out_add_int (xml, "min", first + 1);
	gsf_xml_out_add_int (xml, "max", last + 1);
	gsf_xml_out_add_int (xml, "style", style_id);

	go_xml_out_add_double (xml, "width",
			       (ci ? ci->size_pts : def_width) /
			       ((130. / 18.5703125) * (72./96.)));

	if (ci) {
		if (!ci->visible)
			gsf_xml_out_add_cstr_unchecked (xml, "hidden", "1");
		if (ci->hard_size)
			gsf_xml_out_add_cstr_unchecked (xml, "customWidth", "1");
		else if (fabs (def_width - ci->size_pts) > .1) {
			gsf_xml_out_add_cstr_unchecked (xml, "bestFit", "1");
			gsf_xml_out_add_cstr_unchecked (xml, "customWidth", "1");
		}

		if (ci->outline_level > 0)
			gsf_xml_out_add_int (xml, "outlineLevel",
					     ci->outline_level);
		if (ci->is_collapsed)
			gsf_xml_out_add_cstr_unchecked (xml, "collapsed", "1");
	}

	gsf_xml_out_end_element (xml); /* </col> */
}

static void
xlsx_write_cols (XLSXWriteState *state, GsfXMLOut *xml, GPtrArray *styles)
{
	int first_col = 0, i;
	int last_col = gnm_sheet_get_last_col (state->sheet);
	ColRowInfo const *info = sheet_col_get (state->sheet, first_col);

	gsf_xml_out_start_element (xml, "cols");

	for (i = first_col + 1; i <= last_col; i++) {
		ColRowInfo const *ci = sheet_col_get_info (state->sheet, i);
		if (!col_row_info_equal (info, ci) ||
		    g_ptr_array_index (styles, i) != g_ptr_array_index (styles, i - 1)) {
			xlsx_write_col (state, xml, info,
					first_col, i - 1,
					g_ptr_array_index (styles, i - 1));
			info	  = ci;
			first_col = i;
		}
	}
	xlsx_write_col (state, xml, info,
			first_col, i - 1,
			g_ptr_array_index (styles, i - 1));

	gsf_xml_out_end_element (xml); /* </cols> */
}

static void
xlsx_write_autofilters (XLSXWriteState *state, GsfXMLOut *xml)
{
	GnmFilter const *filter;
	unsigned i;

	if (NULL == state->sheet->filters)
		return;

	/* We write only the first filter per sheet.  */
	filter = state->sheet->filters->data;

	gsf_xml_out_start_element (xml, "autoFilter");
	xlsx_add_range (xml, "ref", &filter->r);

	for (i = 0; i < filter->fields->len ; i++) {
		GnmFilterCondition const *cond =
			gnm_filter_get_condition (filter, i);

		/* filter unused or bucket filters in excel5 */
		if (!cond || cond->op[0] == GNM_FILTER_UNUSED)
			continue;

		gsf_xml_out_start_element (xml, "filterColumn");
		gsf_xml_out_add_int (xml, "colId", i);

		switch (cond->op[0]) {
		case GNM_FILTER_OP_EQUAL:
		case GNM_FILTER_OP_GT:
		case GNM_FILTER_OP_LT:
		case GNM_FILTER_OP_GTE:
		case GNM_FILTER_OP_LTE:
		case GNM_FILTER_OP_NOT_EQUAL: {
			static const char *const opname[6] = {
				"equal", "greaterThan", "lessThan",
				"greaterThanOrEqual", "lessThanOrEqual",
				"notEqual"
			};
			unsigned oi, N;

			for (oi = N = 1; oi < G_N_ELEMENTS (cond->op); oi++) {
				if (cond->op[oi] == GNM_FILTER_UNUSED)
					continue;
				N++;
			}

			gsf_xml_out_start_element (xml, "customFilters");
			if (N > 1)
				gsf_xml_out_add_cstr_unchecked (xml, "and", "true");
			for (oi = 0; oi < G_N_ELEMENTS (cond->op); oi++) {
				GnmFilterOp op = cond->op[oi];
				GString *str;

				if (op == GNM_FILTER_UNUSED)
					continue;
				gsf_xml_out_start_element (xml, "customFilter");
				if (op >= GNM_FILTER_OP_EQUAL && op <= GNM_FILTER_OP_NOT_EQUAL)
					gsf_xml_out_add_cstr_unchecked (xml, "operator", opname[op]);

				str = g_string_new (NULL);
				value_get_as_gstring (cond->value[oi], str, state->convs);
				gsf_xml_out_add_cstr (xml, "val", str->str);
				g_string_free (str, TRUE);
				gsf_xml_out_end_element (xml); /* </customFilter> */
			}
			gsf_xml_out_end_element (xml); /* </customFilters> */
			break;
		}

		case GNM_FILTER_OP_BLANKS :
			gsf_xml_out_start_element (xml, "filters");
			xlsx_add_bool (xml, "blank", TRUE);
			gsf_xml_out_end_element (xml); /* </filters> */
			break;
		case GNM_FILTER_OP_NON_BLANKS :
			gsf_xml_out_start_element (xml, "customFilters");
			gsf_xml_out_start_element (xml, "customFilter");
			gsf_xml_out_add_cstr_unchecked (xml, "operator", "notEqual");
			gsf_xml_out_add_cstr (xml, "val", " ");
			gsf_xml_out_end_element (xml); /* </customFilter> */
			gsf_xml_out_end_element (xml); /* </customFilters> */
			break;

		case GNM_FILTER_OP_TOP_N:
		case GNM_FILTER_OP_BOTTOM_N:
		case GNM_FILTER_OP_TOP_N_PERCENT:
		case GNM_FILTER_OP_BOTTOM_N_PERCENT:
			gsf_xml_out_start_element (xml, "top10");
			go_xml_out_add_double (xml, "val", cond->count);
			if (cond->op[0] & GNM_FILTER_OP_BOTTOM_MASK)
				gsf_xml_out_add_cstr_unchecked (xml, "top", "0");
			if (cond->op[0] & GNM_FILTER_OP_PERCENT_MASK)
				gsf_xml_out_add_cstr_unchecked (xml, "percent", "1");
			gsf_xml_out_end_element (xml); /* </top10> */
			break;

		default:
			continue;
		}

		gsf_xml_out_end_element (xml); /* </filterColumn> */
	}
	gsf_xml_out_end_element (xml); /* </autoFilter> */
}

static void
xlsx_write_protection (XLSXWriteState *state, GsfXMLOut *xml)
{
	gboolean sheet;
	gboolean objects;
	gboolean scenarios;
	gboolean formatCells;
	gboolean formatColumns;
	gboolean formatRows;
	gboolean insertColumns;
	gboolean insertRows;
	gboolean insertHyperlinks;
	gboolean deleteColumns;
	gboolean deleteRows;
	gboolean selectLockedCells;
	gboolean sort;
	gboolean autoFilter;
	gboolean pivotTables;
	gboolean selectUnlockedCells;

	g_object_get (G_OBJECT (state->sheet),
		"protected",				 &sheet,
		"protected-allow-edit-objects",		 &objects,
		"protected-allow-edit-scenarios",	 &scenarios,
		"protected-allow-cell-formatting",	 &formatCells,
		"protected-allow-column-formatting",	 &formatColumns,
		"protected-allow-row-formatting",	 &formatRows,
		"protected-allow-insert-columns",	 &insertColumns,
		"protected-allow-insert-rows",		 &insertRows,
		"protected-allow-insert-hyperlinks",	 &insertHyperlinks,
		"protected-allow-delete-columns",	 &deleteColumns,
		"protected-allow-delete-rows",		 &deleteRows,
		"protected-allow-select-locked-cells",	 &selectLockedCells,
		"protected-allow-sort-ranges",		 &sort,
		"protected-allow-edit-auto-filters",	 &autoFilter,
		"protected-allow-edit-pivottable",	 &pivotTables,
		"protected-allow-select-unlocked-cells", &selectUnlockedCells,
		NULL);

	gsf_xml_out_start_element (xml, "sheetProtection");
	if ( sheet)		  xlsx_add_bool (xml, "sheet",			TRUE);
	if ( objects)		  xlsx_add_bool (xml, "objects",		TRUE);
	if ( scenarios)		  xlsx_add_bool (xml, "scenarios",		TRUE);
	if (!formatCells)	  xlsx_add_bool (xml, "formatCells",		FALSE);
	if (!formatColumns)	  xlsx_add_bool (xml, "formatColumns",		FALSE);
	if (!formatRows)	  xlsx_add_bool (xml, "formatRows",		FALSE);
	if (!insertColumns)	  xlsx_add_bool (xml, "insertColumns",		FALSE);
	if (!insertRows)	  xlsx_add_bool (xml, "insertRows",		FALSE);
	if (!insertHyperlinks)	  xlsx_add_bool (xml, "insertHyperlinks",	FALSE);
	if (!deleteColumns)	  xlsx_add_bool (xml, "deleteColumns",		FALSE);
	if (!deleteRows)	  xlsx_add_bool (xml, "deleteRows",		FALSE);
	if ( selectLockedCells)	  xlsx_add_bool (xml, "selectLockedCells",	TRUE);
	if (!sort)		  xlsx_add_bool (xml, "sort",			FALSE);
	if (!autoFilter)	  xlsx_add_bool (xml, "autoFilter",		FALSE);
	if (!pivotTables)	  xlsx_add_bool (xml, "pivotTables",		FALSE);
	if ( selectUnlockedCells) xlsx_add_bool (xml, "selectUnlockedCells",	TRUE);

	gsf_xml_out_end_element (xml); /* sheetProtection */
}

static void
xlsx_write_breaks (G_GNUC_UNUSED XLSXWriteState *state, GsfXMLOut *xml, GnmPageBreaks *breaks)
{
	unsigned const maxima = (breaks->is_vert ? XLSX_MaxCol : XLSX_MaxRow) - 1;
	GArray const *details = breaks->details;
	GnmPageBreak const *binfo;
	unsigned i;

	gsf_xml_out_start_element (xml,
		(breaks->is_vert) ? "rowBreaks" : "colBreaks");
	gsf_xml_out_add_int (xml, "count", details->len);

	for (i = 0 ; i < details->len ; i++) {
		binfo = &g_array_index (details, GnmPageBreak, i);
		gsf_xml_out_start_element (xml, "brk");
		gsf_xml_out_add_int (xml, "id", binfo->pos);

		/* hard code min=0 max=dir */
		gsf_xml_out_add_int (xml, "max", maxima);

		switch (binfo->type) {
		case GNM_PAGE_BREAK_MANUAL :	gsf_xml_out_add_bool (xml, "man", TRUE); break;
		case GNM_PAGE_BREAK_AUTO :	break;
		case GNM_PAGE_BREAK_NONE :	break;
		case GNM_PAGE_BREAK_DATA_SLICE :gsf_xml_out_add_bool (xml, "pt", TRUE); break;
		}
		gsf_xml_out_end_element (xml); /* </brk> */
	}
	gsf_xml_out_end_element (xml);
}

static int
xlsx_find_paper_code (GtkPaperSize *psize)
{
	XLSXPaperDefs *paper_defs;
	XLSXPaperDefs paper[] =
		{{ 74 , 90 , 90 , 205 , GTK_UNIT_MM },
		 { 38 , 92 , 3.625 , 6.5 , GTK_UNIT_INCH },
		 { 94 , 97 , 97 , 151 , GTK_UNIT_MM },
		 /* { 95 , 97 , 97 , 151 , GTK_UNIT_MM }, */
		 { 37 , 98 , 3.875 , 7.5 , GTK_UNIT_INCH },
		 { 19 , 98 , 3.875 , 8.875 , GTK_UNIT_INCH },
		 { 96 , 102 , 102 , 165 , GTK_UNIT_MM },
		 { 97 , 102 , 102 , 176 , GTK_UNIT_MM },
		 { 20 , 104 , 4.125 , 9.5 , GTK_UNIT_INCH },
		 { 70 , 105 , 105 , 148 , GTK_UNIT_MM },
		 { 92 , 105 , 105 , 235 , GTK_UNIT_MM },
		 { 99 , 110 , 110 , 208 , GTK_UNIT_MM },
		 { 27 , 110 , 110 , 220 , GTK_UNIT_MM },
		 /* { 100 , 110 , 110 , 220 , GTK_UNIT_MM }, */
		 { 36 , 110 , 110 , 230 , GTK_UNIT_MM },
		 { 21 , 114 , 4.5 , 10.375 , GTK_UNIT_INCH },
		 { 31 , 114 , 114 , 162 , GTK_UNIT_MM },
		 { 32 , 114 , 114 , 229 , GTK_UNIT_MM },
		 { 22 , 120 , 4.75 , 11 , GTK_UNIT_INCH },
		 { 101 , 120 , 120 , 230 , GTK_UNIT_MM },
		 { 73 , 120 , 120 , 235 , GTK_UNIT_MM },
		 { 103 , 120 , 120 , 309 , GTK_UNIT_MM },
		 { 98 , 125 , 125 , 176 , GTK_UNIT_MM },
		 { 23 , 127 , 5 , 11.5 , GTK_UNIT_INCH },
		 { 88 , 128 , 128 , 182 , GTK_UNIT_MM },
		 { 6 , 139 , 5.5 , 8.5 , GTK_UNIT_INCH },
		 { 93 , 146 , 146 , 215 , GTK_UNIT_MM },
		 { 81 , 148 , 148 , 100 , GTK_UNIT_MM },
		 { 83 , 148 , 148 , 105 , GTK_UNIT_MM },
		 { 82 , 148 , 148 , 200 , GTK_UNIT_MM },
		 { 11 , 148 , 148 , 210 , GTK_UNIT_MM },
		 /* { 61 , 148 , 148 , 210 , GTK_UNIT_MM }, */
		 { 107 , 151 , 151 , 97 , GTK_UNIT_MM },
		 /* { 108 , 151 , 151 , 97 , GTK_UNIT_MM }, */
		 { 102 , 160 , 160 , 230 , GTK_UNIT_MM },
		 { 28 , 162 , 162 , 229 , GTK_UNIT_MM },
		 { 109 , 165 , 165 , 102 , GTK_UNIT_MM },
		 { 64 , 174 , 174 , 235 , GTK_UNIT_MM },
		 { 110 , 176 , 176 , 102 , GTK_UNIT_MM },
		 { 35 , 176 , 176 , 125 , GTK_UNIT_MM },
		 /* { 111 , 176 , 176 , 125 , GTK_UNIT_MM }, */
		 { 13 , 176 , 176 , 250 , GTK_UNIT_MM },
		 /* { 34 , 176 , 176 , 250 , GTK_UNIT_MM }, */
		 { 89 , 182 , 182 , 128 , GTK_UNIT_MM },
		 { 62 , 182 , 182 , 257 , GTK_UNIT_MM },
		 { 7 , 184 , 7.25 , 10.5 , GTK_UNIT_INCH },
		 { 43 , 200 , 200 , 148 , GTK_UNIT_MM },
		 /* { 69 , 200 , 200 , 148 , GTK_UNIT_MM }, */
		 { 65 , 201 , 201 , 276 , GTK_UNIT_MM },
		 { 87 , 205 , 205 , 90 , GTK_UNIT_MM },
		 { 112 , 208 , 208 , 110 , GTK_UNIT_MM },
		 { 54 , 210 , 8.275 , 11 , GTK_UNIT_INCH },
		 { 78 , 210 , 210 , 148 , GTK_UNIT_MM },
		 { 9 , 210 , 210 , 297 , GTK_UNIT_MM },
		 /* { 10 , 210 , 210 , 297 , GTK_UNIT_MM }, */
		 /* { 55 , 210 , 210 , 297 , GTK_UNIT_MM }, */
		 { 60 , 210 , 210 , 330 , GTK_UNIT_MM },
		 { 1 , 215 , 8.5 , 11 , GTK_UNIT_INCH },
		 /* { 2 , 215 , 8.5 , 11 , GTK_UNIT_INCH }, */
		 /* { 18 , 215 , 8.5 , 11 , GTK_UNIT_INCH }, */
		 { 40 , 215 , 8.5 , 12 , GTK_UNIT_INCH },
		 { 59 , 215 , 8.5 , 12.69 , GTK_UNIT_INCH },
		 { 14 , 215 , 8.5 , 13 , GTK_UNIT_INCH },
		 /* { 41 , 215 , 8.5 , 13 , GTK_UNIT_INCH }, */
		 { 5 , 215 , 8.5 , 14 , GTK_UNIT_INCH },
		 { 106 , 215 , 215 , 146 , GTK_UNIT_MM },
		 { 15 , 215 , 215 , 275 , GTK_UNIT_MM },
		 { 72 , 216 , 216 , 277 , GTK_UNIT_MM },
		 { 113 , 220 , 220 , 110 , GTK_UNIT_MM },
		 { 47 , 220 , 220 , 220 , GTK_UNIT_MM },
		 { 57 , 227 , 227 , 356 , GTK_UNIT_MM },
		 { 44 , 228 , 9 , 11 , GTK_UNIT_INCH },
		 { 30 , 229 , 229 , 324 , GTK_UNIT_MM },
		 /* { 104 , 229 , 229 , 324 , GTK_UNIT_MM }, */
		 { 114 , 230 , 230 , 120 , GTK_UNIT_MM },
		 { 115 , 230 , 230 , 160 , GTK_UNIT_MM },
		 { 50 , 235 , 9.275 , 12 , GTK_UNIT_INCH },
		 /* { 56 , 235 , 9.275 , 12 , GTK_UNIT_INCH }, */
		 { 51 , 235 , 9.275 , 15 , GTK_UNIT_INCH },
		 { 91 , 235 , 235 , 105 , GTK_UNIT_MM },
		 { 86 , 235 , 235 , 120 , GTK_UNIT_MM },
		 { 53 , 236 , 236 , 322 , GTK_UNIT_MM },
		 { 71 , 240 , 240 , 332 , GTK_UNIT_MM },
		 { 12 , 250 , 250 , 353 , GTK_UNIT_MM },
		 /* { 33 , 250 , 250 , 353 , GTK_UNIT_MM }, */
		 { 42 , 250 , 250 , 353 , GTK_UNIT_MM },
		 { 45 , 254 , 10 , 11 , GTK_UNIT_INCH },
		 { 16 , 254 , 10 , 14 , GTK_UNIT_INCH },
		 { 80 , 257 , 257 , 182 , GTK_UNIT_MM },
		 { 85 , 277 , 277 , 216 , GTK_UNIT_MM },
		 { 75 , 279 , 11 , 8.5 , GTK_UNIT_INCH },
		 { 3 , 279 , 11 , 17 , GTK_UNIT_INCH },
		 /* { 17 , 279 , 11 , 17 , GTK_UNIT_INCH }, */
		 { 52 , 296 , 11.69 , 18 , GTK_UNIT_INCH },
		 { 77 , 297 , 297 , 210 , GTK_UNIT_MM },
		 { 8 , 297 , 297 , 420 , GTK_UNIT_MM },
		 /* { 67 , 297 , 297 , 420 , GTK_UNIT_MM }, */
		 { 90 , 304 , 12 , 11 , GTK_UNIT_INCH },
		 { 58 , 305 , 305 , 487 , GTK_UNIT_MM },
		 { 116 , 309 , 309 , 120 , GTK_UNIT_MM },
		 { 63 , 322 , 322 , 445 , GTK_UNIT_MM },
		 /* { 68 , 322 , 322 , 445 , GTK_UNIT_MM }, */
		 { 117 , 324 , 324 , 229 , GTK_UNIT_MM },
		 { 29 , 324 , 324 , 458 , GTK_UNIT_MM },
		 /* { 105 , 324 , 324 , 458 , GTK_UNIT_MM }, */
		 { 84 , 332 , 332 , 240 , GTK_UNIT_MM },
		 { 79 , 364 , 364 , 257 , GTK_UNIT_MM },
		 { 39 , 377 , 14.875 , 11 , GTK_UNIT_INCH },
		 { 46 , 381 , 15 , 11 , GTK_UNIT_INCH },
		 { 76 , 420 , 420 , 297 , GTK_UNIT_MM },
		 { 66 , 420 , 420 , 594 , GTK_UNIT_MM },
		 { 4 , 431 , 17 , 11 , GTK_UNIT_INCH },
		 { 24 , 431 , 17 , 22 , GTK_UNIT_INCH },
		 { 118 , 458 , 458 , 324 , GTK_UNIT_MM },
		 { 25 , 558 , 22 , 34 , GTK_UNIT_INCH },
		 { 26 , 863 , 34 , 44 , GTK_UNIT_INCH },
		 {0,0,0,0,0 }};
	gchar const *psize_name;
	int width_mm;

	psize_name = gtk_paper_size_get_name (psize);
	if (0 == strcmp (psize_name, GTK_PAPER_NAME_LETTER))
		return 1;
	if (0 == strcmp (psize_name, GTK_PAPER_NAME_A4))
		return 9;
	if (0 == strcmp (psize_name, GTK_PAPER_NAME_A3))
		return 8;
	if (0 == strcmp (psize_name, GTK_PAPER_NAME_A5))
		return 11;
	if (0 == strcmp (psize_name, GTK_PAPER_NAME_B5))
		return 13;
	if (0 == strcmp (psize_name, GTK_PAPER_NAME_EXECUTIVE))
		return 7;
	if (0 == strcmp (psize_name, GTK_PAPER_NAME_LEGAL))
		return 5;

	width_mm = (int) gtk_paper_size_get_width (psize, GTK_UNIT_MM);

	for (paper_defs = paper; paper_defs->code > 0; paper_defs++) {
		if (width_mm < paper_defs->width_mm)
			return 0;
		if (width_mm == paper_defs->width_mm) {
			gdouble width = gtk_paper_size_get_width (psize,  paper_defs->unit);
			gdouble height = gtk_paper_size_get_height (psize,  paper_defs->unit);

			if (width == paper_defs->width && height == paper_defs->height)
				return paper_defs->code;
		}
	}
	return 0;
}

static void
xlsx_write_print_info_hf (XLSXWriteState *state, GsfXMLOut *xml,
			  const GnmPrintHF *hf, const char *hftext)
{
	char *s = xls_header_footer_export (hf);

	gsf_xml_out_start_element (xml, hftext);
	gsf_xml_out_add_cstr (xml, NULL, s);
	gsf_xml_out_end_element (xml); /* hftext */

	g_free (s);
}

static void
xlsx_write_print_info (XLSXWriteState *state, GsfXMLOut *xml)
{
	GnmPrintInformation *pi = state->sheet->print_info;
	double h_margin, f_margin;
	double left;
	double right;
	double t_margin, b_margin;

	g_return_if_fail (pi != NULL);

	gsf_xml_out_start_element (xml, "printOptions");
	gsf_xml_out_end_element (xml); /* </printOptions> */

	gsf_xml_out_start_element (xml, "pageMargins");
	print_info_get_margins (pi, &h_margin, &f_margin, &left, &right,
				&t_margin, &b_margin);
	go_xml_out_add_double (xml, "left",	left / 72.);
	go_xml_out_add_double (xml, "right",	right / 72.);
	go_xml_out_add_double (xml, "top",	t_margin / 72.);
	go_xml_out_add_double (xml, "bottom",	b_margin / 72.);
	go_xml_out_add_double (xml, "header",	h_margin / 72.);
	go_xml_out_add_double (xml, "footer",	f_margin / 72.);
	gsf_xml_out_end_element (xml); /* </pageMargins> */

	gsf_xml_out_start_element (xml, "pageSetup");

	xlsx_add_bool (xml, "blackAndWhite", pi->print_black_and_white);
	switch (pi->comment_placement) {
	case GNM_PRINT_COMMENTS_IN_PLACE:
		gsf_xml_out_add_cstr_unchecked (xml, "cellComments", "asDisplayed");
		break;
	case GNM_PRINT_COMMENTS_AT_END:
		gsf_xml_out_add_cstr_unchecked (xml, "cellComments", "atEnd");
		break;
	case GNM_PRINT_COMMENTS_NONE:
	default:
		gsf_xml_out_add_cstr_unchecked (xml, "cellComments", "none");
		break;
	}
	if (pi->n_copies > 0)
		gsf_xml_out_add_int (xml, "copies", pi->n_copies);
	xlsx_add_bool (xml, "draft", pi->print_as_draft);
	switch (pi->error_display) {
	case GNM_PRINT_ERRORS_AS_BLANK:
		gsf_xml_out_add_cstr_unchecked (xml, "errors", "blank");
		break;
	case GNM_PRINT_ERRORS_AS_DASHES:
		gsf_xml_out_add_cstr_unchecked (xml, "errors", "dash");
		break;
	case GNM_PRINT_ERRORS_AS_NA:
		gsf_xml_out_add_cstr_unchecked (xml, "errors", "NA");
		break;
	case GNM_PRINT_ERRORS_AS_DISPLAYED:
	default:
		gsf_xml_out_add_cstr_unchecked (xml, "errors", "displayed");
		break;
	}
	if (pi->start_page >= 0)
		gsf_xml_out_add_int (xml, "firstPageNumber", pi->start_page);
	if (pi->scaling.dim.rows != 1)
		gsf_xml_out_add_int (xml, "fitToHeight", pi->scaling.dim.rows);
	if (pi->scaling.dim.cols != 1)
		gsf_xml_out_add_int (xml, "fitToWidth", pi->scaling.dim.cols);
	/* horizontalDpi skipped */
	/* id skipped */

	if (pi->page_setup) {
		GtkPageOrientation orient;

		orient = gtk_page_setup_get_orientation (pi->page_setup);
		switch (orient) {
		case GTK_PAGE_ORIENTATION_PORTRAIT:
		case GTK_PAGE_ORIENTATION_REVERSE_PORTRAIT:
			gsf_xml_out_add_cstr_unchecked (xml, "orientation", "portrait");
			break;
		case GTK_PAGE_ORIENTATION_LANDSCAPE:
		case GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE:
			gsf_xml_out_add_cstr_unchecked (xml, "orientation", "landscape");
			break;
		default:
			gsf_xml_out_add_cstr_unchecked (xml, "orientation", "default");
			break;
		}
	} else
		gsf_xml_out_add_cstr_unchecked (xml, "orientation", "default");

	gsf_xml_out_add_cstr_unchecked
		(xml, "pageOrder",
		 pi->print_across_then_down ? "overThenDown" : "downThenOver");

	if (pi->page_setup) {
		GtkPaperSize *psize;
		int paper_code;

		psize = gtk_page_setup_get_paper_size (pi->page_setup);
		paper_code = xlsx_find_paper_code (psize);

		if (paper_code > 0)
			gsf_xml_out_add_int (xml, "paperSize", paper_code);
		else {
			gdouble width = gtk_paper_size_get_width (psize, GTK_UNIT_POINTS);
			gdouble height = gtk_paper_size_get_height (psize, GTK_UNIT_POINTS);

			xlsx_add_pt (xml, "paperHeight", height);
			xlsx_add_pt (xml, "paperWidth", width);
		}
	}

	if (pi->scaling.percentage.x > 0)
		gsf_xml_out_add_int (xml, "scale",
				     (int)CLAMP (pi->scaling.percentage.x, 10, 400));
	xlsx_add_bool (xml, "useFirstPageNumber", (pi->start_page >= 0));
	/* usePrinterDefaults skipped */
	/* verticalDpi skipped */

	gsf_xml_out_end_element (xml); /* </pageSetup> */

	gsf_xml_out_start_element (xml, "headerFooter");
	xlsx_write_print_info_hf (state, xml, pi->header, "oddHeader");
	xlsx_write_print_info_hf (state, xml, pi->footer, "oddFooter");
	gsf_xml_out_end_element (xml); /* </headerFooter> */

	if (NULL != pi->page_breaks.v)
		xlsx_write_breaks (state, xml, pi->page_breaks.v);
	if (NULL != pi->page_breaks.h)
		xlsx_write_breaks (state, xml, pi->page_breaks.h);
}

/**********************************************************************/

static int
by_val_int (G_GNUC_UNUSED gpointer key_a, gpointer val_a,
	     G_GNUC_UNUSED gpointer key_b, gpointer val_b,
	     G_GNUC_UNUSED gpointer user)
{
	return GPOINTER_TO_INT (val_a) - GPOINTER_TO_INT (val_b);
}

static void
write_comment_author (gpointer key, G_GNUC_UNUSED gpointer value, GsfXMLOut *xml)
{
	gsf_xml_out_start_element (xml, "author");
	gsf_xml_out_add_cstr_unchecked (xml, NULL, (char const *) key);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_comments (XLSXWriteState *state, GsfOutput *sheet_part, GSList *objects)
{
	GsfXMLOut *xml;
	GHashTable *authors;
	unsigned author = 0;
	char const *authorname;
	GSList *ptr;
	SheetObjectAnchor const *anchor;
	PangoAttrList *attrs;
	char *name = g_strdup_printf ("comments%u.xml", ++state->comment);
	GsfOutput *comments_part = gsf_outfile_new_child_full (state->xl_dir, name, FALSE,
		"content-type", "application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml",
		NULL);
	g_free (name);
	gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (comments_part),
		GSF_OUTFILE_OPEN_PKG (sheet_part), ns_rel_com);
	xml = gsf_xml_out_new (comments_part);
	gsf_xml_out_start_element (xml, "comments");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	/* search for comments authors */
	authors = g_hash_table_new (g_str_hash, g_str_equal);
	for (ptr = objects; ptr; ptr = ptr->next) {
		authorname = cell_comment_author_get (GNM_CELL_COMMENT (ptr->data));
		if (authorname != NULL && !g_hash_table_lookup_extended (authors, authorname, NULL, NULL))
			g_hash_table_insert (authors, (gpointer) authorname, GUINT_TO_POINTER (author++));
	}
	/* save authors */
	gsf_xml_out_start_element (xml, "authors");
	gnm_hash_table_foreach_ordered (authors, (GHFunc) write_comment_author,
					by_val_int, xml);
	gsf_xml_out_end_element (xml); /* </authors> */
	/* save comments */
	gsf_xml_out_start_element (xml, "commentList");
	for (ptr = objects; ptr; ptr = ptr->next) {
		gsf_xml_out_start_element (xml, "comment");
		anchor = sheet_object_get_anchor (ptr->data);
		gsf_xml_out_add_cstr_unchecked (xml, "ref", range_as_string (&anchor->cell_bound));
		authorname = cell_comment_author_get (GNM_CELL_COMMENT (ptr->data));
		if (authorname != NULL)
			gsf_xml_out_add_uint (xml, "authorId",
					      GPOINTER_TO_UINT (g_hash_table_lookup (authors, authorname)));
		gsf_xml_out_start_element (xml, "text");
		/* Save text as rich text */
		g_object_get (ptr->data, "text", &name, "markup", &attrs, NULL);
		if (name && *name)
			xlsx_write_rich_text (xml, name, attrs, TRUE);
		g_free (name);
		pango_attr_list_unref (attrs);
		gsf_xml_out_end_element (xml); /* </text> */
		gsf_xml_out_end_element (xml); /* </comment> */
	}
	gsf_xml_out_end_element (xml); /* </commentList> */
	g_hash_table_destroy (authors);
	gsf_xml_out_end_element (xml); /* </comments> */
	g_object_unref (xml);
	gsf_output_close (comments_part);
	g_object_unref (comments_part);
}

#include "xlsx-write-drawing.c"

static char const *
xlsx_write_sheet (XLSXWriteState *state, GsfOutfile *wb_part, Sheet *sheet)
{
	char *name = g_strdup_printf ("sheet%u.xml", ++state->sheet_dir.count);
	GsfOutput *sheet_part = gsf_outfile_new_child_full
		(xlsx_dir_get (&state->sheet_dir), name, FALSE,
		 "content-type", "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml",
		 NULL);
	char const *rId = gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (sheet_part),
		GSF_OUTFILE_OPEN_PKG (wb_part),
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet");
	GsfXMLOut *xml;
	GnmRange  extent, cell_extent;
	GSList   *drawing_objs, *legacy_drawing_objs, *comments, *others, *objects, *p;
	char const *chart_drawing_rel_id = NULL;
	char const *legacy_drawing_rel_id = NULL;
	GPtrArray *col_styles;
	GnmPrintInformation *pi = NULL;
	GHashTable *zorder;
	int z;
	gboolean ext_tab_textcolor = FALSE;

	state->sheet = sheet;
	col_styles = sheet_style_most_common (state->sheet, TRUE);
	excel_sheet_extent (state->sheet, &extent, col_styles,
			    XLSX_MAX_COLS, XLSX_MAX_ROWS, state->io_context);
	cell_extent = sheet_get_cells_extent (state->sheet);
	extent = range_union (&extent, &cell_extent);

	objects = sheet_objects_get (state->sheet, NULL, G_TYPE_NONE);
	drawing_objs = legacy_drawing_objs = comments = others = NULL;
	zorder = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (p = objects, z = 1; p; p = p->next, z++) {
		SheetObject *so = p->data;

		g_hash_table_insert (zorder, so, GINT_TO_POINTER (z));

		if (GNM_IS_CELL_COMMENT (so)) {
			comments = g_slist_prepend (comments, so);
			legacy_drawing_objs = g_slist_prepend (legacy_drawing_objs, so);
		} else if (GNM_IS_SO_GRAPH (so) ||
			 GNM_IS_SO_LINE (so) ||
			 GNM_IS_SO_FILLED (so) ||
			 GNM_IS_SO_IMAGE (so))
			drawing_objs = g_slist_prepend (drawing_objs, so);
		else if (GNM_IS_SOW_SCROLLBAR (so) || GNM_IS_SOW_SLIDER (so) ||
			 GNM_IS_SOW_SPINBUTTON (so) ||
			 GNM_IS_SOW_BUTTON (so) ||
			 GNM_IS_SOW_RADIO_BUTTON (so) ||
			 GNM_IS_SOW_CHECKBOX (so) ||
			 GNM_IS_SOW_COMBO (so) ||
			 GNM_IS_SOW_LIST (so))
			legacy_drawing_objs = g_slist_prepend (legacy_drawing_objs, so);
		else if (GNM_IS_FILTER_COMBO (so))
			; /* Nothing here */
		else
			others = g_slist_prepend (others, so);
	}
	g_slist_free (objects);

	if (comments) {
		comments = g_slist_reverse (comments);
		xlsx_write_comments (state, sheet_part, comments);
		g_slist_free (comments);
	}

	if (drawing_objs) {
		drawing_objs = g_slist_reverse (drawing_objs);
		chart_drawing_rel_id = xlsx_write_drawing_objects (state, sheet_part, drawing_objs, zorder);
		g_slist_free (drawing_objs);
	}

	if (legacy_drawing_objs) {
		legacy_drawing_objs = g_slist_reverse (legacy_drawing_objs);
		legacy_drawing_rel_id = xlsx_write_legacy_drawing_objects (state, sheet_part, legacy_drawing_objs, zorder);
		g_slist_free (legacy_drawing_objs);
	}

	for (p = others; p; p = p->next) {
		SheetObject *so = p->data;
		char *name;

		g_object_get (so, "name", &name, NULL);
		g_warning ("Not exporting object %s of type %s",
			   (name ? name : "?"),
			   g_type_name (G_OBJECT_TYPE (so)));
		g_free (name);
	}
	g_slist_free (others);

	g_hash_table_destroy (zorder);

	xml = gsf_xml_out_new (sheet_part);
/* CT_Worksheet =                                          */
	gsf_xml_out_start_element (xml, "worksheet");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:r", ns_rel);
	if (state->with_extension)
		gsf_xml_out_add_cstr_unchecked (xml, "xmlns:gnmx", ns_gnm_ext);

	/*   element sheetPr { CT_SheetPr }?,     */
	gsf_xml_out_start_element (xml, "sheetPr");

	if (NULL != state->sheet->tab_color) {
		gsf_xml_out_start_element (xml, "tabColor");
		xlsx_add_rgb (xml, "rgb", state->sheet->tab_color->go_color);
		gsf_xml_out_end_element (xml); /* </tabColor> */
	}
	if (NULL != state->sheet->tab_text_color)
		ext_tab_textcolor = TRUE;

	pi = state->sheet->print_info;
	if (pi != NULL) {
		gsf_xml_out_start_element (xml, "pageSetUpPr");
		xlsx_add_bool (xml, "fitToPage", pi->scaling.type == PRINT_SCALE_FIT_PAGES);
		gsf_xml_out_end_element (xml); /* </pageSetUpPr> */
	}

	gsf_xml_out_end_element (xml); /* </sheetPr> */

/*   element dimension { CT_SheetDimension }?,     */
	gsf_xml_out_start_element (xml, "dimension");
	xlsx_add_range (xml, "ref", &extent);
	gsf_xml_out_end_element (xml); /* </dimension> */
/*   element sheetViews { CT_SheetViews }?,     */
	gsf_xml_out_start_element (xml, "sheetViews");
	SHEET_FOREACH_VIEW (state->sheet, sv, xlsx_write_sheet_view (xml, sv););
	gsf_xml_out_end_element (xml); /* </sheetViews> */
/*   element sheetFormatPr { CT_SheetFormatPr }?,     */
	gsf_xml_out_start_element (xml, "sheetFormatPr");
	go_xml_out_add_double (xml, "defaultColWidth",
		sheet_col_get_default_size_pts (state->sheet));
	go_xml_out_add_double (xml, "defaultRowHeight",
		sheet_row_get_default_size_pts (state->sheet));
	if (state->sheet->rows.max_outline_level > 0)
		gsf_xml_out_add_int (xml, "outlineLevelRow",
			state->sheet->rows.max_outline_level);
	if (state->sheet->cols.max_outline_level > 0)
		gsf_xml_out_add_int (xml, "outlineLevelCol",
			state->sheet->cols.max_outline_level);
	gsf_xml_out_end_element (xml); /* </sheetFormatPr> */
/*   element cols { CT_Cols }*,     */
	xlsx_write_cols (state, xml, col_styles);
/*   element sheetData { CT_SheetData },     */
	xlsx_write_cells (state, xml, &extent, col_styles);
/*   element sheetCalcPr { CT_SheetCalcPr }?,     */
/*   element sheetProtection { CT_SheetProtection }?,     */
	xlsx_write_protection (state, xml);
/*   element protectedRanges { CT_ProtectedRanges }?,     */
/*   element scenarios { CT_Scenarios }?,     */
/*   element autoFilter { CT_AutoFilter }?,     */
	xlsx_write_autofilters (state, xml);
/*   element sortState { CT_SortState }?,     */
/*   element dataConsolidate { CT_DataConsolidate }?,     */
/*   element customSheetViews { CT_CustomSheetViews }?,     */
/*   element mergeCells { CT_MergeCells }?,     */
	xlsx_write_merges (state, xml);
/*   element phoneticPr { CT_PhoneticPr }?,     */
/*   element conditionalFormatting { CT_ConditionalFormatting }*,     */
	xlsx_write_conditional_formatting (state, xml);
/*   element dataValidations { CT_DataValidations }?,     */
	xlsx_write_validations (state, xml, &extent);
/*   element hyperlinks { CT_Hyperlinks }?,     */
	xlsx_write_hlinks (state, xml, &extent);
/*   element printOptions { CT_PrintOptions }?, included in xlsx_write_print_info */
/*   element pageMargins { CT_PageMargins }?,   included in xlsx_write_print_info */
/*   element pageSetup { CT_PageSetup }?,       included in xlsx_write_print_info */
/*   element headerFooter { CT_HeaderFooter }?, included in xlsx_write_print_info */
	xlsx_write_print_info (state, xml);
/*   element rowBreaks { CT_PageBreak }?,     */
/*   element colBreaks { CT_PageBreak }?,     */
/*   element customProperties { CT_CustomProperties }?,     */
/*   element cellWatches { CT_CellWatches }?,     */
/*   element ignoredErrors { CT_IgnoredErrors }?,     */
/*   element smartTags { CT_SmartTags }?,     */
/*   element drawing { CT_Drawing }?,     */
	if (NULL != chart_drawing_rel_id) {
		gsf_xml_out_start_element (xml, "drawing");
		gsf_xml_out_add_cstr_unchecked (xml, "r:id", chart_drawing_rel_id);
		gsf_xml_out_end_element (xml);  /* </drawing> */
	}
/*   element legacyDrawing { CT_LegacyDrawing }?,  Deleted in edition 2   */
	if (NULL != legacy_drawing_rel_id) {
		gsf_xml_out_start_element (xml, "legacyDrawing");
		gsf_xml_out_add_cstr_unchecked (xml, "r:id", legacy_drawing_rel_id);
		gsf_xml_out_end_element (xml);  /* </legacyDrawing> */
	}
/*   element legacyDrawingHF { CT_LegacyDrawing }?,  Deleted in edition 2   */
/*   element picture { CT_SheetBackgroundPicture }?,     */
/*   element oleObjects { CT_OleObjects }?,     */
/*   element controls { CT_Controls }?,     */
/*   element webPublishItems { CT_WebPublishItems }?,     */
/*   element tableParts { CT_TableParts }?,     */

	if (state->with_extension && ext_tab_textcolor) {
		gsf_xml_out_start_element (xml, "extLst");
		gsf_xml_out_start_element (xml, "ext");
		gsf_xml_out_add_cstr_unchecked (xml, "uri", ns_gnm_ext);

		gsf_xml_out_start_element (xml, "gnmx:tabTextColor");
		xlsx_add_rgb (xml, "rgb", state->sheet->tab_text_color->go_color);
		gsf_xml_out_end_element (xml); /* </gnmx:tabTextColor> */

		gsf_xml_out_end_element (xml);  /* "ext" */
		gsf_xml_out_end_element (xml);  /* "extLst" */
	}

/*   element extLst { CT_ExtensionList }?     */
	gsf_xml_out_end_element (xml); /* </worksheet> */

	g_object_unref (xml);
	gsf_output_close (sheet_part);
	g_object_unref (sheet_part);
	g_free (name);
	g_ptr_array_free (col_styles, TRUE);

	state->sheet = NULL;

	return rId;
}

static void
xlsx_write_named_expression (G_GNUC_UNUSED gpointer key, GnmNamedExpr *nexpr, XLSXClosure *closure)
{
	char *formula;

	g_return_if_fail (nexpr != NULL);
	if (!expr_name_is_active (nexpr))
		return;

	gsf_xml_out_start_element (closure->xml, "definedName");

	if (nexpr->is_permanent) {
		char const *expr_name = expr_name_name (nexpr);
		if (0 == strcmp (expr_name, "Print_Area"))
			gsf_xml_out_add_cstr (closure->xml, "name", "_xlnm.Print_Area");
		else if (0 == strcmp (expr_name, "Sheet_Title"))
			gsf_xml_out_add_cstr (closure->xml, "name", "_xlnm.Sheet_Title");
		else
			gsf_xml_out_add_cstr (closure->xml, "name", expr_name);
	} else {
		gsf_xml_out_add_cstr (closure->xml, "name", expr_name_name (nexpr));
	}
	if (nexpr->pos.sheet != NULL)
		gsf_xml_out_add_int (closure->xml, "localSheetId",
				     nexpr->pos.sheet->index_in_wb);

	formula = expr_name_as_string (nexpr, NULL, closure->state->convs);
	gsf_xml_out_add_cstr (closure->xml, NULL, formula);
	g_free (formula);

	gsf_xml_out_end_element (closure->xml);
}

static void
xlsx_write_definedNames (XLSXWriteState *state, GsfXMLOut *xml)
{
	XLSXClosure closure = {state, xml};

	gsf_xml_out_start_element (xml, "definedNames");
	workbook_foreach_name
		(state->base.wb, FALSE,
		 (GHFunc)&xlsx_write_named_expression, &closure);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_calcPR (XLSXWriteState *state, GsfXMLOut *xml)
{
	Workbook const *wb = state->base.wb;

#warning Filter by defaults
	gsf_xml_out_start_element (xml, "calcPr");

	gsf_xml_out_add_cstr_unchecked (xml, "calcMode",
		wb->recalc_auto ? "auto" : "manual");

	xlsx_add_bool (xml, "iterate", wb->iteration.enabled);
	gsf_xml_out_add_int (xml, "iterateCount",
		wb->iteration.max_number);
	go_xml_out_add_double (xml, "iterateDelta",
		wb->iteration.tolerance);

	gsf_xml_out_end_element (xml);
}

#include "xlsx-write-pivot.c"

#include "xlsx-write-docprops.c"

static gboolean
rich_value_equal (GnmValue const *a, GnmValue const *b)
{
	return value_equal (a, b) &&
		go_format_eq (VALUE_FMT (a), VALUE_FMT (b));
}

static guint
rich_value_hash (GnmValue const *v)
{
	return value_hash (v) ^ GPOINTER_TO_UINT (VALUE_FMT (v));
}

static void
xlsx_write_workbook (XLSXWriteState *state, GsfOutfile *root_part)
{
	int i;
	GsfXMLOut  *xml;
	GSList	   *cacheRefs;
	GPtrArray  *sheetIds  = g_ptr_array_new ();
	GsfOutfile *xl_dir    = (GsfOutfile *)gsf_outfile_new_child (root_part, "xl", TRUE);
	GsfOutfile *wb_part   = (GsfOutfile *)gsf_outfile_open_pkg_add_rel (xl_dir, "workbook.xml",
		"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
		root_part,
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument");
	GnmStyle *style = gnm_style_new_default ();

	state->xl_dir = xl_dir;
	state->shared_string_hash = g_hash_table_new_full
		((GHashFunc)rich_value_hash, (GEqualFunc)rich_value_equal,
		 (GDestroyNotify)value_release, NULL);
	state->shared_string_array = g_ptr_array_new ();
	state->styles_hash = g_hash_table_new_full
		(gnm_style_hash, (GEqualFunc)gnm_style_equal,
		 (GDestroyNotify)gnm_style_unref, NULL);
	state->styles_array = g_ptr_array_new ();
	state->dxfs_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
	state->dxfs_array = g_ptr_array_new ();
	state->axids = g_hash_table_new (NULL, NULL);

	xlsx_get_style_id (state, style);
	gnm_style_unref (style);

	state->convs	 = xlsx_conventions_new (TRUE);
	xlsx_dir_init (&state->sheet_dir, state->xl_dir, "worksheets");
	xlsx_dir_init (&state->chart_dir, state->xl_dir, "charts");
	xlsx_dir_init (&state->drawing_dir, state->xl_dir, "drawings");
	xlsx_dir_init (&state->legacy_drawing_dir, NULL, NULL);
	xlsx_dir_init (&state->media_dir, state->xl_dir, "media");
	xlsx_dir_init (&state->pivotCache_dir, state->xl_dir, "pivotCache");
	xlsx_dir_init (&state->pivotTable_dir, state->xl_dir, "pivotTable");

	g_ptr_array_set_size (sheetIds, workbook_sheet_count (state->base.wb));
	for (i = 0 ; i < workbook_sheet_count (state->base.wb); i++) {
		Sheet *sheet = workbook_sheet_by_index (state->base.wb, i);
		const char *rId = xlsx_write_sheet (state, wb_part, sheet);
		g_ptr_array_index (sheetIds, i) = (gpointer)rId;
	}

	xlsx_write_shared_strings (state, wb_part);
	xlsx_write_styles (state, wb_part);
	xlsx_write_docprops (state, root_part);
	cacheRefs = xlsx_write_pivots (state, wb_part);

	xml = gsf_xml_out_new (GSF_OUTPUT (wb_part));
	gsf_xml_out_start_element (xml, "workbook");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:r", ns_rel);
	/* Note the schema does not allow the attribute xml:space */

	gsf_xml_out_start_element (xml, "fileVersion");
	gsf_xml_out_add_int (xml, "lastEdited", 4);
	gsf_xml_out_add_int (xml, "lowestEdited", 4);
	gsf_xml_out_add_int (xml, "rupBuild", 3820);
	gsf_xml_out_end_element (xml);

	gsf_xml_out_start_element (xml, "workbookPr");
	gsf_xml_out_add_int (xml, "date1904",
			     workbook_date_conv (state->base.wb)->use_1904
			     ? 1 : 0);
	gsf_xml_out_end_element (xml);

	gsf_xml_out_start_element (xml, "bookViews");
	WORKBOOK_FOREACH_VIEW (state->base.wb, view, {
		int scale = 10;  /* Guess */
		gsf_xml_out_start_element (xml, "workbookView");
		gsf_xml_out_add_int (xml, "activeTab",
			view->current_sheet->index_in_wb);
		if (view->preferred_width > 0)
			gsf_xml_out_add_int (xml, "windowWidth", view->preferred_width * scale);
		if (view->preferred_height > 0)
			gsf_xml_out_add_int (xml, "windowHeight", view->preferred_height * scale);
		gsf_xml_out_end_element (xml);
	});
	gsf_xml_out_end_element (xml);

	gsf_xml_out_start_element (xml, "sheets");
	for (i = 0 ; i < workbook_sheet_count (state->base.wb); i++) {
		Sheet const *sheet = workbook_sheet_by_index (state->base.wb, i);
		gsf_xml_out_start_element (xml, "sheet");
		gsf_xml_out_add_cstr (xml, "name", sheet->name_unquoted);
		gsf_xml_out_add_int (xml, "sheetId", i+1);	/* FIXME What is this ?? */
		gsf_xml_out_add_cstr_unchecked (xml, "r:id",
			g_ptr_array_index (sheetIds, i));
		gsf_xml_out_end_element (xml); /* </sheet> */
	}
	gsf_xml_out_end_element (xml); /* </sheets> */

	xlsx_write_definedNames (state, xml);

	xlsx_write_calcPR (state, xml);

	if (NULL != cacheRefs) {
		GSList *ptr;
		unsigned int i = 0;
		gsf_xml_out_start_element (xml, "pivotCaches");
		for (ptr = cacheRefs ; ptr != NULL ; ptr = ptr->next) {
			gsf_xml_out_start_element (xml, "pivotCache");
			gsf_xml_out_add_int (xml, "cacheId", i++);
			gsf_xml_out_add_cstr_unchecked (xml, "r:id", ptr->data);
			gsf_xml_out_end_element (xml); /* </pivotCache> */
		}
		gsf_xml_out_end_element (xml); /* </pivotCaches> */
		g_slist_free (cacheRefs);
	}
	gsf_xml_out_start_element (xml, "webPublishing");
	xlsx_add_bool (xml, "allowPng", TRUE);
	xlsx_add_bool (xml, "css", FALSE);
	if (state->version == ECMA_376_2006)
		gsf_xml_out_add_int (xml, "codePage", 1252); /* FIXME : Use utf-8 ? */
	else
		gsf_xml_out_add_cstr_unchecked (xml, "characterSet", "UTF-8");
	gsf_xml_out_end_element (xml);

	gsf_xml_out_end_element (xml); /* </workbook> */
	g_object_unref (xml);

	xlsx_conventions_free (state->convs);
	g_hash_table_destroy (state->shared_string_hash);
	g_ptr_array_free (state->shared_string_array, TRUE);
	g_hash_table_destroy (state->styles_hash);
	g_ptr_array_free (state->styles_array, TRUE);
	g_hash_table_destroy (state->dxfs_hash);
	g_ptr_array_free (state->dxfs_array, TRUE);
	g_hash_table_destroy (state->axids);

	xlsx_dir_close (&state->sheet_dir);
	xlsx_dir_close (&state->chart_dir);
	xlsx_dir_close (&state->drawing_dir);
	xlsx_dir_close (&state->legacy_drawing_dir);
	xlsx_dir_close (&state->media_dir);
	xlsx_dir_close (&state->pivotCache_dir);
	xlsx_dir_close (&state->pivotTable_dir);

	gsf_output_close (GSF_OUTPUT (wb_part));
	g_object_unref (wb_part);

	gsf_output_close (GSF_OUTPUT (xl_dir));
	g_object_unref (xl_dir);

	g_ptr_array_free (sheetIds, TRUE);
}

G_MODULE_EXPORT void
xlsx_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		gconstpointer wb_view, GsfOutput *output);
void
xlsx_file_save (G_GNUC_UNUSED GOFileSaver const *fs, GOIOContext *io_context,
		gconstpointer wb_view, GsfOutput *output)
{
	XLSXWriteState state;
	GsfOutfile *root_part;
	GnmLocale  *locale;
	GsfOutfile *zip;

	locale = gnm_push_C_locale ();

	state.version           = ECMA_376_2006;
	state.with_extension    = TRUE;
	state.io_context	= io_context;
	state.base.wb		= wb_view_get_workbook (wb_view);
	state.comment		= 0;
	state.custom_prop_id    = 29;
	state.drawing_elem_id   = 1024;

	zip = gsf_outfile_zip_new (output, NULL);
	root_part = gsf_outfile_open_pkg_new (zip);
	g_object_unref (zip);

	xlsx_write_workbook (&state, root_part);
	gsf_output_close (GSF_OUTPUT (root_part));
	g_object_unref (root_part);

	gnm_pop_C_locale (locale);
}

G_MODULE_EXPORT void
xlsx2_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		gconstpointer wb_view, GsfOutput *output);
void
xlsx2_file_save (G_GNUC_UNUSED GOFileSaver const *fs, GOIOContext *io_context,
		gconstpointer wb_view, GsfOutput *output)
{
	XLSXWriteState state;
	GsfOutfile *root_part;
	GnmLocale  *locale;
	GsfOutfile *zip;

	locale = gnm_push_C_locale ();
	state.version           = ECMA_376_2008;
	state.with_extension    = TRUE;
	state.io_context	= io_context;
	state.base.wb		= wb_view_get_workbook (wb_view);
	state.comment		= 0;
	state.custom_prop_id    = 29;
	state.drawing_elem_id   = 1024;

	zip = gsf_outfile_zip_new (output, NULL);
	root_part = gsf_outfile_open_pkg_new (zip);
	g_object_unref (zip);

	xlsx_write_workbook (&state, root_part);
	gsf_output_close (GSF_OUTPUT (root_part));
	g_object_unref (root_part);

	gnm_pop_C_locale (locale);
}

