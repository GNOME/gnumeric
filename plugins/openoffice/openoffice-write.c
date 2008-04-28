/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * openoffice-write.c : export OpenOffice OASIS .ods files
 *
 * Copyright (C) 2004-2006 Jody Goldberg (jody@gnome.org)
 *
 * Copyright (C) 2006 Andreas J. Guelzow (aguelzow@pyrshep.ca)
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

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-outfile.h>
#include <gsf/gsf-outfile-zip.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-opendoc-utils.h>
#include <goffice/utils/go-glib-extras.h>

#include <glib/gi18n-lib.h>

#define MANIFEST "manifest:"
#define OFFICE	 "office:"
#define STYLE	 "style:"
#define TABLE	 "table:"
#define TEXT     "text:"
#define DUBLINCORE "dc:"

typedef struct {
	GsfXMLOut *xml;
	IOContext *ioc;
	WorkbookView const *wbv;
	Workbook const	   *wb;
	GnmConventions *conv;
} GnmOOExport;

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
	{ "xmlns:dom",		"http://www.w3.org/2001/xml-events" },
	{ "xmlns:xforms",	"http://www.w3.org/2002/xforms" },
	{ "xmlns:xsd",		"http://www.w3.org/2001/XMLSchema" },
	{ "xmlns:xsi",		"http://www.w3.org/2001/XMLSchema-instance" },
};

static void
odf_write_mimetype (GnmOOExport *state, GsfOutput *child)
{
	gsf_output_puts (child, "application/vnd.oasis.opendocument.spreadsheet");
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

	gsf_xml_out_start_element (state->xml, STYLE "properties");
	odf_add_bool (state->xml, TABLE "display",
		sheet->visibility == GNM_SHEET_VISIBILITY_VISIBLE);
	gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "writing-mode",
		sheet->text_is_rtl ? "rl-tb" : "lr-tb");
	gsf_xml_out_end_element (state->xml); /* </style:properties> */

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
		char *rendered_string;

		if (gnm_cell_is_array (cell)) {
			GnmExprArrayCorner const *ac;
			char *formula, *eq_formula;
			GnmParsePos pp;

			ac = gnm_expr_top_get_array_corner (cell->base.texpr);
			if (ac != NULL) {
				gsf_xml_out_add_uint (state->xml,
					TABLE "number-matrix-columns-spanned",
						     (unsigned int)(ac->cols));
				gsf_xml_out_add_uint (state->xml,
					TABLE "number-matrix-rows-spanned",
						     (unsigned int)(ac->rows));
			}

			parse_pos_init_cell (&pp, cell);
			formula = gnm_expr_top_as_string (cell->base.texpr,
							  &pp,
							  state->conv);
			eq_formula = g_strdup_printf ("oooc:=%s", formula);

#if 0
			g_warning ("Writing: %s", eq_formula);
#endif

			gsf_xml_out_add_cstr (state->xml,
					      TABLE "formula",
					      eq_formula);
			g_free (formula);
			g_free (eq_formula);

		}

		rendered_string = gnm_cell_get_rendered_text (cell);

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
		case VALUE_ARRAY:
		case VALUE_ERROR:
		case VALUE_CELLRANGE:
		default:
			break;
			gsf_xml_out_add_cstr_unchecked (state->xml,
							OFFICE "value-type", "string");
			gsf_xml_out_add_cstr (state->xml,
					      OFFICE "value",
					      value_peek_string (cell->value));
		}
		gsf_xml_out_start_element (state->xml, TEXT "p");
		gsf_xml_out_add_cstr (state->xml, NULL, rendered_string);
		gsf_xml_out_end_element (state->xml);   /* p */

		g_free (rendered_string);
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
	GnmStyle *col_styles [SHEET_MAX_COLS];
	GnmRange  extent;
	int max_cols = gnm_sheet_get_max_cols (sheet);
	int max_rows = gnm_sheet_get_max_rows (sheet);
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
		gsf_xml_out_add_int (state->xml, TABLE "number-rows-repeated",
				     extent.start.row);
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
		if (covered_cell > 0)
			odf_write_covered_cell (state, &covered_cell);

		gsf_xml_out_end_element (state->xml);   /* table-row */
	}

	go_slist_free_custom (sheet_merges, g_free);
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
}
