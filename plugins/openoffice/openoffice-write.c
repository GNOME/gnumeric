/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * openoffice-write.c : export OpenOffice OASIS .ods files
 *
 * Copyright (C) 2004 Jody Goldberg (jody@gnome.org)
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
#include <print-info.h>
#include <print-info.h>
#include <parse-util.h>
#include <tools/scenarios.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-outfile.h>
#include <gsf/gsf-outfile-zip.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-opendoc-utils.h>

#include <glib/gi18n-lib.h>
#include <locale.h>

#define MANIFEST "manifest:"
#define OFFICE	 "office:"
#define STYLE	 "style:"
#define TABLE	 "table:"
#define TEXT     "text:"

typedef struct {
	GsfXMLOut *xml;
	IOContext *ioc;
	WorkbookView const *wbv;
	Workbook const	   *wb;
	GnmExprConventions *conv;
} GnmOOExport;

	static struct {
		char const *key;
		char const *url;
	} const ns[] = {
		{ "xmlns:office",
		  "urn:oasis:names:tc:opendocument:xmlns:office:1.0" },
		{ "xmlns:style",
		  "urn:oasis:names:tc:opendocument:xmlns:style:1.0"},
		{ "xmlns:text",
		  "urn:oasis:names:tc:opendocument:xmlns:text:1.0" },
		{ "xmlns:table",
		  "urn:oasis:names:tc:opendocument:xmlns:table:1.0" },
		{ "xmlns:draw",
		  "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" },
		{ "xmlns:fo",
		  "urn:oasis:names:tc:opendocument:xmlns:"
		  "xsl-fo-compatible:1.0"},
		{ "xmlns:xlink", "http://www.w3.org/1999/xlink" },
		{ "xmlns:dc", "http://purl.org/dc/elements/1.1/" },
		{ "xmlns:meta",
		  "urn:oasis:names:tc:opendocument:xmlns:meta:1.0" },
		{ "xmlns:number",
		  "urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0" },
		{ "xmlns:svg",
		  "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" },
		{ "xmlns:chart",
		  "urn:oasis:names:tc:opendocument:xmlns:chart:1.0" },
		{ "xmlns:dr3d",
		  "urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0" },
		{ "xmlns:math", "http://www.w3.org/1998/Math/MathML" },
		{ "xmlns:form",
		  "urn:oasis:names:tc:opendocument:xmlns:form:1.0" },
		{ "xmlns:script",
		  "urn:oasis:names:tc:opendocument:xmlns:script:1.0" },
		{ "xmlns:ooo", "http://openoffice.org/2004/office" },
		{ "xmlns:ooow", "http://openoffice.org/2004/writer" },
		{ "xmlns:oooc", "http://openoffice.org/2004/calc" },
		{ "xmlns:dom", "http://www.w3.org/2001/xml-events" },
		{ "xmlns:xforms", "http://www.w3.org/2002/xforms" },
		{ "xmlns:xsd", "http://www.w3.org/2001/XMLSchema" },
		{ "xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance" },
	};




void	openoffice_file_save (GOFileSaver const *fs, IOContext *ioc,
			      WorkbookView const *wbv, GsfOutput *output);

static void
oo_write_mimetype (GnmOOExport *state, GsfOutput *child)
{
	gsf_output_puts (child, "application/vnd.oasis.opendocument.spreadsheet");
}

/*****************************************************************************/

static void
oo_start_style (GsfXMLOut *xml, char const *name, char const *family)
{
	gsf_xml_out_start_element (xml, STYLE "style");
	gsf_xml_out_add_cstr_unchecked (xml, STYLE "name", name);
	gsf_xml_out_add_cstr_unchecked (xml, STYLE "family", family);
}
static void
oo_write_table_styles (GnmOOExport *state)
{
	oo_start_style (state->xml, "ta1", "table");
	gsf_xml_out_add_cstr_unchecked (state->xml, STYLE "master-page-name", "Default");

	gsf_xml_out_start_element (state->xml, STYLE "properties");
	gsf_xml_out_add_bool (state->xml, TABLE "display", TRUE);
	gsf_xml_out_end_element (state->xml); /* </style:properties> */

	gsf_xml_out_end_element (state->xml); /* </style:style> */
}

static GnmExprConventions *
oo_expr_conventions_new (void)
{
	GnmExprConventions *conv;

	conv = gnm_expr_conventions_new ();
	conv->argument_sep_semicolon = TRUE;
	conv->decimal_sep_dot = TRUE;
	
	/* FIXME: we have to set up the correct conventions */
	return conv;
}


static gboolean
od_cell_is_covered (Sheet const *sheet, GnmCell *current_cell, 
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
od_write_empty_cell (GnmOOExport *state, int *num)
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
od_write_covered_cell (GnmOOExport *state, int *num)
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
od_write_cell (GnmOOExport *state, GnmCell *cell, GnmRange const *merge_range)
{
	char *rendered_string;
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
		if (cell_has_expr(cell)) {
			char *formula, *eq_formula;
			GnmParsePos pp;
			parse_pos_init_cell (&pp, cell);
			formula = gnm_expr_as_string (cell->base.texpr->expr,
						      &pp,
						      state->conv);
			eq_formula = g_strdup_printf ("oooc:=%s", formula);
			
			gsf_xml_out_add_cstr_unchecked (state->xml,
							TABLE "formula",
							eq_formula);
			g_free (formula);
			g_free (eq_formula);
		}
		
		rendered_string = cell_get_rendered_text (cell);
		
		switch (cell->value->type) {
		case VALUE_BOOLEAN:
			gsf_xml_out_add_cstr_unchecked (state->xml, 
							OFFICE "value-type", "boolean");
			gsf_xml_out_add_bool (state->xml, OFFICE "boolean-value", 
					       value_get_as_bool
					       (cell->value, NULL));
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
			gsf_xml_out_add_cstr_unchecked (state->xml, 
							OFFICE "value",rendered_string);
			break;
		case VALUE_ARRAY:   /* FIX ME */
			break;
			gsf_xml_out_add_cstr_unchecked (state->xml, 
							OFFICE "value-type", "string");
			gsf_xml_out_add_cstr_unchecked (state->xml, 
							OFFICE "value",
							value_peek_string (cell->value));
			break;
		case VALUE_ERROR:
		case VALUE_CELLRANGE:
		default:
			break;
			gsf_xml_out_add_cstr_unchecked (state->xml, 
							OFFICE "value-type", "string");
			gsf_xml_out_add_cstr_unchecked (state->xml, 
							OFFICE "value",
							value_peek_string (cell->value));
		}
		
		gsf_xml_out_start_element (state->xml, TEXT "p");
		gsf_xml_out_add_cstr_unchecked (state->xml, NULL, rendered_string);
		gsf_xml_out_end_element (state->xml);   /* p */
	}
	gsf_xml_out_end_element (state->xml);   /* table-cell */	

	g_free (rendered_string);
}

static void
cb_sheet_merges_free (gpointer data, gpointer user_data)
{
	if (data != NULL)
		g_free (data);
}

static void
oo_write_sheet (GnmOOExport *state, Sheet const *sheet)
{
	GnmStyle *col_styles [SHEET_MAX_COLS];
	GnmRange  extent;
	int max_cols = SHEET_MAX_COLS;
	int max_rows = SHEET_MAX_ROWS;
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
	gsf_xml_out_add_int (state->xml, TABLE "number-columns-repeated", extent.end.col);
	gsf_xml_out_end_element (state->xml); /* table-column */
	
	if (extent.start.row > 0) {
		/* We need to write a bunch of empty rows !*/
		gsf_xml_out_start_element (state->xml, TABLE "table-row");
		gsf_xml_out_add_int (state->xml, TABLE "number-rows-repeated", extent.start.row);
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

			pos.col = col;
			merge_range = sheet_merge_is_corner (sheet, &pos);

			if (od_cell_is_covered (sheet, current_cell, col, row,
						merge_range, &sheet_merges)) {
				if (null_cell >0)
					od_write_empty_cell (state, &null_cell);
				covered_cell++;
				continue;
			}
			if ((merge_range == NULL) && cell_is_empty (current_cell)) {
				if (covered_cell > 0)
					od_write_covered_cell (state, &covered_cell);
				null_cell++;
				continue;
			}

			if (null_cell > 0)
				od_write_empty_cell (state, &null_cell);
			if (covered_cell > 0)
				od_write_covered_cell (state, &covered_cell);
			od_write_cell (state, current_cell, merge_range);
			
		}
		if (covered_cell > 0)
			od_write_covered_cell (state, &covered_cell);
		
		gsf_xml_out_end_element (state->xml);   /* table-row */
	}

	if (sheet_merges != NULL) {
		g_slist_foreach (sheet_merges, cb_sheet_merges_free, NULL);
		g_slist_free (sheet_merges);
	}
}

static void
oo_write_content (GnmOOExport *state, GsfOutput *child)
{
	int i;

	state->xml = gsf_xml_out_new (child);
	gsf_xml_out_set_doc_type (state->xml, "\n");
	gsf_xml_out_start_element (state->xml, OFFICE "document-content");

	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
/*	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "class", "spreadsheet"); */
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version", "1.0");

	gsf_xml_out_simple_element (state->xml, OFFICE "scripts", NULL);

	gsf_xml_out_start_element (state->xml, OFFICE "font-face-decls");
	gsf_xml_out_end_element (state->xml); /* </office:font-face-decls> */

	gsf_xml_out_start_element (state->xml, OFFICE "automatic-styles");
 	oo_write_table_styles (state); 
	gsf_xml_out_end_element (state->xml); /* </office:automatic-styles> */

	gsf_xml_out_start_element (state->xml, OFFICE "body");
  	gsf_xml_out_start_element (state->xml, OFFICE "spreadsheet");  
	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet *sheet = workbook_sheet_by_index (state->wb, i);
#warning validate sheet name against OOo conventions
		gsf_xml_out_start_element (state->xml, TABLE "table");
		gsf_xml_out_add_cstr (state->xml, TABLE "name", sheet->name_unquoted);
		gsf_xml_out_add_cstr (state->xml, TABLE "style-name", "ta1");
		oo_write_sheet (state, sheet);
		gsf_xml_out_end_element (state->xml); /* </table:table> */
	}
  	gsf_xml_out_end_element (state->xml); /* </office:spreadsheet> */  
	gsf_xml_out_end_element (state->xml); /* </office:body> */

	gsf_xml_out_end_element (state->xml); /* </office:document-content> */
	g_object_unref (state->xml);
	state->xml = NULL;
}

/*****************************************************************************/

static void
oo_write_styles (GnmOOExport *state, GsfOutput *child)
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
oo_write_meta (GnmOOExport *state, GsfOutput *child)
{
	GsfXMLOut *xml = gsf_xml_out_new (child);
	gsf_opendoc_metadata_write (xml, 
		go_doc_get_meta_data (GO_DOC (state->wb)));
	g_object_unref (xml);
}

/*****************************************************************************/

static void
oo_write_settings (GnmOOExport *state, GsfOutput *child)
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
oo_file_entry (GsfXMLOut *out, char const *type, char const *name)
{
	gsf_xml_out_start_element (out, MANIFEST "file-entry");
	gsf_xml_out_add_cstr (out, MANIFEST "media-type", type);
	gsf_xml_out_add_cstr (out, MANIFEST "full-path", name);
	gsf_xml_out_end_element (out); /* </manifest:file-entry> */
}

static void
oo_write_manifest (GnmOOExport *state, GsfOutput *child)
{
	GsfXMLOut *xml = gsf_xml_out_new (child);
	gsf_xml_out_set_doc_type (xml, "\n");
	gsf_xml_out_start_element (xml, MANIFEST "manifest");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:manifest",
		"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0");
	oo_file_entry (xml, "application/vnd.oasis.opendocument.spreadsheet" ,"/");
	oo_file_entry (xml, "", "Pictures/");
	oo_file_entry (xml, "text/xml", "content.xml");
	oo_file_entry (xml, "text/xml", "styles.xml");
	oo_file_entry (xml, "text/xml", "meta.xml");
	oo_file_entry (xml, "text/xml", "settings.xml");
	gsf_xml_out_end_element (xml); /* </manifest:manifest> */
	g_object_unref (xml);
}

/**********************************************************************************/

void
openoffice_file_save (GOFileSaver const *fs, IOContext *ioc,
		      WorkbookView const *wbv, GsfOutput *output)
{
	static struct {
		void (*func) (GnmOOExport *state, GsfOutput *child);
		char const *name;
	} const streams[] = {
		{ oo_write_mimetype,	"mimetype" },
		{ oo_write_content,	"content.xml" },
		{ oo_write_styles,	"styles.xml" },
		{ oo_write_meta,	"meta.xml" },
		{ oo_write_settings,	"settings.xml" },
		{ oo_write_manifest,	"META-INF/manifest.xml" }
	};

	GnmOOExport state;
	char *old_num_locale, *old_monetary_locale;
	GsfOutfile *outfile = NULL;
	GsfOutput  *child;
	GError *err;
	unsigned i;

	old_num_locale = g_strdup (go_setlocale (LC_NUMERIC, NULL));
	go_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (go_setlocale (LC_MONETARY, NULL));
	go_setlocale (LC_MONETARY, "C");
	go_set_untranslated_bools ();

	outfile = gsf_outfile_zip_new (output, &err);

	state.ioc = ioc;
	state.wbv = wbv;
	state.wb  = wb_view_get_workbook (wbv);
	state.conv = oo_expr_conventions_new ();
	for (i = 0 ; i < G_N_ELEMENTS (streams); i++) {
		child = gsf_outfile_new_child  (outfile, streams[i].name, FALSE);
		streams[i].func (&state, child);
		gsf_output_close (child);
		g_object_unref (G_OBJECT (child));
	}

	gsf_output_close (GSF_OUTPUT (outfile));
	g_object_unref (G_OBJECT (outfile));

	/* go_setlocale restores bools to locale translation */
	go_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	go_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);

	g_free (state.conv);
}
