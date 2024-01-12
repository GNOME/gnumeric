/*
 * boot.c: the external interface to the MS Excel import/export
 *
 * Copyright (C) 2000-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 1998-2001 Michael Meeks (miguel@kernel.org)
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

#include <libgnumeric.h>
#include <command-context.h>
#include <workbook-view.h>
#include <workbook.h>
#include <gnm-plugin.h>

#include "excel.h"
#include "ms-excel-write.h"
#include "boot.h"
#include "ms-excel-util.h"
#include "ms-excel-read.h"

#include <goffice/goffice.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-msole.h>
#include <gsf/gsf-infile-msvba.h>
#include <gsf/gsf-msole-utils.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-outfile.h>
#include <gsf/gsf-outfile-msole.h>
#include <gsf/gsf-structured-blob.h>
#include <glib/gi18n-lib.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

/* Used to toggle debug messages on & off */
/*
 * As a convention
 * 0 = quiet, no experimental features.
 * 1 = enable experimental features
 * >1 increasing levels of detail.
 */
gint ms_excel_read_debug = 0;
gint ms_excel_pivot_debug = 0;
gint ms_excel_escher_debug = 0;
gint ms_excel_formula_debug = 0;
gint ms_excel_chart_debug = 0;
gint ms_excel_write_debug = 0;
gint ms_excel_object_debug = 0;

gboolean excel_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl);
void excel_file_open (GOFileOpener const *fo, GOIOContext *context, WorkbookView *wbv, GsfInput *input);
void excel_enc_file_open (GOFileOpener const *fo, char const *enc, GOIOContext *context, WorkbookView *wbv, GsfInput *input);
void excel_biff7_file_save (GOFileSaver const *fs, GOIOContext *context, WorkbookView const *wbv, GsfOutput *output);
void excel_biff8_file_save (GOFileSaver const *fs, GOIOContext *context, WorkbookView const *wbv, GsfOutput *output);
void excel_dsf_file_save   (GOFileSaver const *fs, GOIOContext *context, WorkbookView const *wbv, GsfOutput *output);

static GsfInput *
find_content_stream (GsfInfile *ole, gboolean *is_97)
{
	static char const * const stream_names[] = {
		"Workbook",	"WORKBOOK",	"workbook",
		"Book",		"BOOK",		"book"
	};
	GsfInput *stream;
	unsigned i;

	for (i = 0 ; i < G_N_ELEMENTS (stream_names) ; i++) {
		stream = gsf_infile_child_by_name (ole, stream_names[i]);
		if (stream != NULL) {
			if (is_97 != NULL)
				*is_97 = (i < 3);
			return stream;
		}
	}

	return  NULL;
}

gboolean
excel_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl)
{
	GsfInfile *ole;
	GsfInput  *stream;
	gboolean res = FALSE;

	if (input == NULL)
		return FALSE;
	ole = gsf_infile_msole_new (input, NULL);
	if (ole == NULL) {	/* Test for non-OLE BIFF file */
		guint8 const *data;
		gsf_input_seek (input, 0, G_SEEK_SET);
		data = gsf_input_read (input, 2, NULL);
		return data && data[0] == 0x09 && (data[1] & 0xf1) == 0;
	}

	stream = find_content_stream (ole, NULL);
	if (stream != NULL) {
		g_object_unref (stream);
		res = TRUE;
	}
	g_object_unref (ole);

	return res;
}

static void
excel_read_metadata (GsfDocMetaData *meta_data, GsfInfile *ole, char const *name,
		     GOIOContext *context)
{
	GsfInput *stream = gsf_infile_child_by_name (ole, name);

	if (stream != NULL) {
		GError *err = gsf_doc_meta_data_read_from_msole (meta_data, stream);
		if (err != NULL) {
			go_io_warning (context, "%s", err->message);
			g_error_free (err);
		}

		g_object_unref (stream);
	}
}

#ifdef SPEW_VBA
static void
cb_dump_vba (char const *name, guint8 const *src_code)
{
	g_printerr ("<module name=\"%s\">\n<![CDATA[%s]]>\n</module>\n", name, src_code);
}
#endif

/* Service entry point */
void
excel_enc_file_open (GOFileOpener const *fo, char const *enc, GOIOContext *context,
		     WorkbookView *wbv, GsfInput *input)
{
	GsfInput  *stream = NULL;
	GError    *err = NULL;
	GsfInfile *ole = gsf_infile_msole_new (input, &err);
	Workbook  *wb = wb_view_get_workbook (wbv);
	gboolean   is_double_stream_file, is_97;
	GsfDocMetaData *meta_data;

	if (ole == NULL) {
		guint8 const *data;

		/* Test for non-OLE BIFF file */
		gsf_input_seek (input, 0, G_SEEK_SET);
		data = gsf_input_read (input, 2, NULL);
		if (data && data[0] == 0x09 && (data[1] & 0xf1) == 0) {
			gsf_input_seek (input, -2, G_SEEK_CUR);
			excel_read_workbook (context, wbv, input,
					     &is_double_stream_file, enc);
			/* NOTE : we lack a saver for the early formats */
			g_clear_error (&err);
			return;
		}

		/* OK, it really isn't an Excel file */
		g_return_if_fail (err != NULL);
		go_cmd_context_error_import (GO_CMD_CONTEXT (context),
			err->message);
		g_error_free (err);
		return;
	}

	stream = find_content_stream (ole, &is_97);
	if (stream == NULL) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (context),
			 _("No Workbook or Book streams found."));
		g_object_unref (ole);
		return;
	}

	excel_read_workbook (context, wbv, stream, &is_double_stream_file, enc);
	g_object_unref (stream);

	meta_data = gsf_doc_meta_data_new ();
	excel_read_metadata (meta_data, ole, "\05SummaryInformation", context);
	excel_read_metadata (meta_data, ole, "\05DocumentSummaryInformation", context);
	go_doc_set_meta_data (GO_DOC (wb), meta_data);
	g_object_unref (meta_data);

	/* See if there are any macros to keep around */
	stream = gsf_infile_child_by_name (ole, "\01CompObj");
	if (stream != NULL) {
		GsfInput *macros = gsf_infile_child_by_name (ole, "_VBA_PROJECT_CUR");
		if (macros != NULL) {
			GsfInput *vba_child = gsf_infile_child_by_name (GSF_INFILE (macros), "VBA");
			GsfInfile *vba = vba_child
				? gsf_infile_msvba_new (GSF_INFILE (vba_child), NULL)
				: NULL;
			GsfStructuredBlob *blob;

			if (NULL != vba) {
				GHashTable *modules =
					gsf_infile_msvba_steal_modules (GSF_INFILE_MSVBA (vba));
				if (NULL != modules) {
#ifdef SPEW_VBA
					g_hash_table_foreach (modules,
						(GHFunc) cb_dump_vba, NULL);
#endif
					g_object_set_data_full (G_OBJECT (wb), "VBA",
						modules, (GDestroyNotify) g_hash_table_destroy);
				}
				g_object_unref (vba);
			}
			if (vba_child)
				g_object_unref (vba_child);

			blob = gsf_structured_blob_read (stream);
			if (blob)
				g_object_set_data_full (G_OBJECT (wb),
							"MS_EXCEL_COMPOBJ_STREAM",
							blob, g_object_unref);

			blob = gsf_structured_blob_read (macros);
			if (blob)
				g_object_set_data_full (G_OBJECT (wb),
							"MS_EXCEL_MACROS",
							blob, g_object_unref);

			g_object_unref (macros);
		}
		g_object_unref (stream);
	}

	stream = gsf_infile_child_by_name (ole, "\01Ole");
	if (stream) {
		GsfStructuredBlob *blob = gsf_structured_blob_read (stream);
		if (blob)
			g_object_set_data_full (G_OBJECT (wb), "MS_EXCEL_OLE_STREAM",
						blob, g_object_unref);
		g_object_unref (stream);
	}

	g_object_unref (ole);

	/* simple guess of format based on stream names */
	if (is_double_stream_file)
		workbook_set_saveinfo (wb, GO_FILE_FL_AUTO,
			go_file_saver_for_id ("Gnumeric_Excel:excel_dsf"));
	else if (is_97)
		workbook_set_saveinfo (wb, GO_FILE_FL_AUTO,
			go_file_saver_for_id ("Gnumeric_Excel:excel_biff8"));
	else
		workbook_set_saveinfo (wb, GO_FILE_FL_AUTO,
			go_file_saver_for_id ("Gnumeric_Excel:excel_biff7"));
}

void
excel_file_open (GOFileOpener const *fo, GOIOContext *context,
                 WorkbookView *wbv, GsfInput *input)
{
	excel_enc_file_open (fo, NULL, context, wbv, input);
}

static void
excel_save (GOIOContext *context, WorkbookView const *wbv, GsfOutput *output,
	    gboolean biff7, gboolean biff8)
{
	Workbook *wb;
	GsfOutput *content;
	GsfOutfile *outfile;
	ExcelWriteState *ewb = NULL;
	GsfStructuredBlob *blob;
	GsfDocMetaData *meta_data;

	go_io_progress_message (context, _("Preparing to save..."));
	go_io_progress_range_push (context, 0.0, 0.1);
	ewb = excel_write_state_new (context, wbv, biff7, biff8);
	go_io_progress_range_pop (context);
	if (ewb == NULL)
		return;

	wb = wb_view_get_workbook (wbv);
	outfile = gsf_outfile_msole_new (output);
	ewb->export_macros = (biff8 &&
		NULL != g_object_get_data (G_OBJECT (wb), "MS_EXCEL_MACROS"));

	go_io_progress_message (context, _("Saving file..."));
	go_io_progress_range_push (context, 0.1, 1.0);
	if (biff7)
		excel_write_v7 (ewb, outfile);
	if (biff8)
		excel_write_v8 (ewb, outfile);
	excel_write_state_free (ewb);
	go_io_progress_range_pop (context);

	meta_data = go_doc_get_meta_data (GO_DOC (wb));
	if (meta_data != NULL) {
		content = gsf_outfile_new_child (outfile,
			"\05DocumentSummaryInformation", FALSE);
		gsf_doc_meta_data_write_to_msole (meta_data, content, TRUE);
		gsf_output_close (content);
		g_object_unref (content);

		content = gsf_outfile_new_child (outfile,
			"\05SummaryInformation", FALSE);
		gsf_doc_meta_data_write_to_msole (meta_data, content, FALSE);
		gsf_output_close (content);
		g_object_unref (content);
	}

	/* restore the macros we loaded */
	blob = g_object_get_data (G_OBJECT (wb), "MS_EXCEL_COMPOBJ_STREAM");
	if (blob != NULL)
		gsf_structured_blob_write (blob, outfile);

	blob = g_object_get_data (G_OBJECT (wb), "MS_EXCEL_OLE_STREAM");
	if (blob != NULL)
		gsf_structured_blob_write (blob, outfile);

	blob = g_object_get_data (G_OBJECT (wb), "MS_EXCEL_MACROS");
	if (blob)
		gsf_structured_blob_write (blob, outfile);

	gsf_output_close (GSF_OUTPUT (outfile));
	g_object_unref (outfile);
}

void
excel_dsf_file_save (GOFileSaver const *fs, GOIOContext *context,
		     WorkbookView const *wbv, GsfOutput *output)
{
	excel_save (context, wbv, output, TRUE, TRUE);
}
void
excel_biff8_file_save (GOFileSaver const *fs, GOIOContext *context,
		       WorkbookView const *wbv, GsfOutput *output)
{
	excel_save (context, wbv, output, FALSE, TRUE);
}

void
excel_biff7_file_save (GOFileSaver const *fs, GOIOContext *context,
		       WorkbookView const *wbv, GsfOutput *output)
{
	excel_save (context, wbv, output, TRUE, FALSE);
}


#include <formula-types.h>
G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	excel_read_init ();

#if 0
{
	int i;
	char const *name;

	for (i = 0 ; i < excel_func_desc_size; i++) {
		ExcelFuncDesc const *fd = excel_func_desc + i;
		name = fd->name;
		if (fd->flags & (XL_UNKNOWN | XL_MAGIC))
			continue;
		if (fd->flags & XL_XLM) {
			if (fd->flags != XL_XLM)
				g_printerr ("%s : flags in addition to XLM\n", name);
			if (fd->min_args != fd->max_args)
				g_printerr ("%s : min != max\n", name);
			continue;
		}
		if (fd->min_args < 0)
			g_printerr ("%s : min_args < 0\n", name);
		if (fd->max_args < 0)
			g_printerr ("%s : min_args < 0\n", name);
		if (fd->known_args != NULL &&
		    fd->num_known_args != strlen (fd->known_args))
			g_printerr ("%s : num_expected_args inconsistent\n", name);
	}
}
#endif
}

/*
 * Cleanup allocations made by this plugin.
 * (Called right before we are unloaded.)
 */
G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	destroy_xl_font_widths ();
	excel_read_cleanup ();
}
