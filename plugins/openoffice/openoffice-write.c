/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * openoffice-write.c : export OpenOffice OASIS .sxc files
 *
 * Copyright (C) 2004 Jody Goldberg (jody@gnome.org)
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
#include <summary.h>
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
#include <tools/scenarios.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-outfile.h>
#include <gsf/gsf-outfile-zip.h>
#include <gsf/gsf-utils.h>
#include <glib/gi18n.h>
#include <locale.h>

#define MANIFEST "manifest:"
#define OFFICE	 "office:"
#define STYLE	 "style:"
#define TABLE	 "table:"

typedef struct {
	GsfXMLOut *xml;
	IOContext *ioc;
	WorkbookView const *wbv;
	Workbook const	   *wb;
} GnmOOExport;

void	openoffice_file_save (GOFileSaver const *fs, IOContext *ioc,
			      WorkbookView const *wbv, GsfOutput *output);

static void
oo_write_mimetype (GnmOOExport *state, GsfOutput *child)
{
	gsf_output_puts (child, "application/vnd.sun.xml.calc");
}

/*****************************************************************************/

static unsigned oo_max_cols (void) { return 256; }
static unsigned oo_max_rows (void) { return 32000; }

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

static void
oo_write_sheet (GnmOOExport *state, Sheet const *sheet)
{
	GnmStyle *col_styles [SHEET_MAX_COLS];
	GnmRange  extent;
	int max_cols = oo_max_cols ();
	int max_rows = oo_max_rows ();
	int i;

	extent = sheet_get_extent (sheet, FALSE);
	if (extent.end.row >= max_rows) {
		gnm_io_warning (state->ioc,
			_("Some content will be lost when saving as OpenOffice .sxc. "
			  "It only supports %d rows, and sheet '%s' has %d"),
			max_rows, sheet->name_unquoted, extent.end.row);
		extent.end.row = max_rows;
	}
	if (extent.end.col >= max_cols) {
		gnm_io_warning (state->ioc,
			_("Some content will be lost when saving as OpenOffice .sxc. "
			  "It only supports %d columns, and sheet '%s' has %d"),
			max_cols, sheet->name_unquoted, extent.end.col);
		extent.end.col = max_cols;
	}

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
	/* It is ok to have formatting out of range, we can disregard that. */
	if (extent.end.col > max_cols)
		extent.end.col = max_cols;
	if (extent.end.row > max_rows)
		extent.end.row = max_rows;
}

static void
oo_write_content (GnmOOExport *state, GsfOutput *child)
{
	static struct {
		char const *key;
		char const *url;
	} const ns[] = {
		{ "xmlns:office",	"http://openoffice.org/2000/office" },
		{ "xmlns:style",	"http://openoffice.org/2000/style" },
		{ "xmlns:text",		"http://openoffice.org/2000/text" },
		{ "xmlns:table",	"http://openoffice.org/2000/table" },
		{ "xmlns:draw",		"http://openoffice.org/2000/drawing" },
		{ "xmlns:fo",		"http://www.w3.org/1999/XSL/Format" },
		{ "xmlns:xlink",	"http://www.w3.org/1999/xlink" },
		{ "xmlns:number",	"http://openoffice.org/2000/datastyle" },
		{ "xmlns:svg",		"http://www.w3.org/2000/svg" },
		{ "xmlns:chart",	"http://openoffice.org/2000/chart" },
		{ "xmlns:dr3d",		"http://openoffice.org/2000/dr3d" },
		{ "xmlns:math",		"http://www.w3.org/1998/Math/MathML" },
		{ "xmlns:form",		"http://openoffice.org/2000/form" },
		{ "xmlns:script",	"http://openoffice.org/2000/script" },
	};
	int i;

	state->xml = gsf_xml_out_new (child);
	gsf_xml_out_set_doc_type (state->xml, 
		"<!DOCTYPE "
		   "office:document-content "
		   "PUBLIC "
		   "\"-//OpenOffice.org//DTD "
		   "OfficeDocument "
		   "1.0//EN\" \"office.dtd\">");
	gsf_xml_out_start_element (state->xml, OFFICE "document-content");

	for (i = 0 ; i < (int)G_N_ELEMENTS (ns) ; i++)
		gsf_xml_out_add_cstr_unchecked (state->xml, ns[i].key, ns[i].url);
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "class", "spreadsheet");
	gsf_xml_out_add_cstr_unchecked (state->xml, OFFICE "version", "1.0");

	gsf_xml_out_simple_element (state->xml, OFFICE "script", NULL);

	gsf_xml_out_start_element (state->xml, OFFICE "font-decls");
	gsf_xml_out_end_element (state->xml); /* </office:font-decls> */

	gsf_xml_out_start_element (state->xml, OFFICE "automatic-styles");
	oo_write_table_styles (state);
	gsf_xml_out_end_element (state->xml); /* </office:automatic-styles> */

	gsf_xml_out_start_element (state->xml, OFFICE "body");
	for (i = 0; i < workbook_sheet_count (state->wb); i++) {
		Sheet *sheet = workbook_sheet_by_index (state->wb, i);
#warning validate sheet name against OOo conventions
		gsf_xml_out_start_element (state->xml, TABLE "table");
		gsf_xml_out_add_cstr (state->xml, TABLE "name", sheet->name_unquoted);
		oo_write_sheet (state, sheet);
		gsf_xml_out_end_element (state->xml); /* </table:table> */
	}
	gsf_xml_out_end_element (state->xml); /* </office:automatic-styles> */

	gsf_xml_out_end_element (state->xml); /* </office:document-content> */
}

/*****************************************************************************/

static void
oo_write_styles (GnmOOExport *state, GsfOutput *child)
{
}

/*****************************************************************************/

static void
oo_write_meta (GnmOOExport *state, GsfOutput *child)
{
}

/*****************************************************************************/

static void
oo_write_settings (GnmOOExport *state, GsfOutput *child)
{
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
	gsf_xml_out_set_doc_type (xml, 
		"<!DOCTYPE "
		   "manifest:manifest "
		   "PUBLIC "
		   "\"-//OpenOffice.org//DTD "
		   "Manifest "
		   "1.0//EN\" \"Manifest.dtd\">");
	gsf_xml_out_start_element (xml, MANIFEST "manifest");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:manifest",
		"http://openoffice.org/2001/manifest");
	oo_file_entry (xml, "application/vnd.sun.xml.calc" ,"/");
	oo_file_entry (xml, "", "Pictures/");
	oo_file_entry (xml, "text/xml", "content.xml");
	oo_file_entry (xml, "text/xml", "styles.xml");
	oo_file_entry (xml, "text/xml", "meta.xml");
	oo_file_entry (xml, "text/xml", "settings.xml");
	gsf_xml_out_end_element (xml); /* </manifest:manifest> */
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
	state.wb  = wb_view_workbook (wbv);
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
}
