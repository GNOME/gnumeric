/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * boot.c: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-2003 Michael Meeks, Jody Goldberg
 **/
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>

#include "file.h"
#include "libgnumeric.h"
#include "io-context.h"
#include "command-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

#include "excel.h"
#include "ms-excel-write.h"
#include "boot.h"
#include "ms-excel-util.h"
#include "ms-excel-read.h"
#include "excel-xml-read.h"

#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-msole.h>
#include <gsf/gsf-msole-utils.h>

#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-outfile.h>
#include <gsf/gsf-outfile-msole.h>
#include <gsf/gsf-structured-blob.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/* Used to toggle debug messages on & off */
/*
 * As a convention
 * 0 = quiet, no experimental features.
 * 1 = enable experimental features
 * >1 increasing levels of detail.
 */
/* Enables debugging mesgs while reading excel workbooks */
gint ms_excel_read_debug = 0;
/* Enables debugging mesgs while reading parsing escher streams */
gint ms_excel_escher_debug = 0;
/* Enables debugging mesgs while reading excel functions */
gint ms_excel_formula_debug = 0;
/* Enables debugging mesgs while reading excel charts */
gint ms_excel_chart_debug = 0;
/* Enables debugging mesgs while writing excel workbooks */
gint ms_excel_write_debug = 0;
/* Enables debugging mesgs while reading excel objects */
gint ms_excel_object_debug = 0;

gboolean excel_file_probe (GnmFileOpener const *fo, GsfInput *input, FileProbeLevel pl);
void excel_file_open (GnmFileOpener const *fo, IOContext *context, WorkbookView *wbv, GsfInput *input);
void excel_biff7_file_save (GnmFileSaver const *fs, IOContext *context, WorkbookView const *wbv, GsfOutput *output);
void excel_biff8_file_save (GnmFileSaver const *fs, IOContext *context, WorkbookView const *wbv, GsfOutput *output);
void excel_dsf_file_save   (GnmFileSaver const *fs, IOContext *context, WorkbookView const *wbv, GsfOutput *output);
void plugin_cleanup (void);

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
excel_file_probe (GnmFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
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
		g_object_unref (G_OBJECT (stream));
		res = TRUE;
	}
	g_object_unref (G_OBJECT (ole));

	return res;
}

static void
excel_read_metadata (Workbook  *wb, GsfInfile *ole, char const *name,
		     IOContext *context)
{
	GError   *err = NULL;
	GsfInput *stream = gsf_infile_child_by_name (ole, name);

	if (stream != NULL) {
		gsf_msole_metadata_read (stream, &err);
		if (err != NULL) {
			gnm_io_warning (context, err->message);
			g_error_free (err);
		}
		gsf_input_seek (stream, 0, G_SEEK_SET);
		g_object_set_data_full (G_OBJECT (wb), name,
			gsf_structured_blob_read (stream), g_object_unref);
		g_object_unref (G_OBJECT (stream));
	}
}

/*
 * excel_file_open
 * @fo:         File opener
 * @context:   	IO context
 * @wbv:    	Workbook view
 * @input:  	Input stream
 *
 * Load en excel workbook.
 */
void
excel_file_open (GnmFileOpener const *fo, IOContext *context,
                 WorkbookView *wbv, GsfInput *input)
{
	GsfInput  *stream = NULL;
	GError    *err = NULL;
	GsfInfile *ole = gsf_infile_msole_new (input, &err);
	Workbook  *wb = wb_view_workbook (wbv);
	gboolean   is_double_stream_file, is_97;

	if (ole == NULL) {
		guint8 const *data;

		/* Test for non-OLE BIFF file */
		gsf_input_seek (input, 0, G_SEEK_SET);
		data = gsf_input_read (input, 2, NULL);
		if (data && data[0] == 0x09 && (data[1] & 0xf1) == 0) {
			gsf_input_seek (input, -2, G_SEEK_CUR);
			excel_read_workbook (context, wbv, input,
					     &is_double_stream_file);
			/* NOTE : we lack a saver for the early formats */
			return;
		}

		/* OK, it really isn't an Excel file */
		g_return_if_fail (err != NULL);
		gnm_cmd_context_error_import (GNM_CMD_CONTEXT (context),
			err->message);
		g_error_free (err);
		return;
	}

	stream = find_content_stream (ole, &is_97);
	if (stream == NULL) {
		gnm_cmd_context_error_import (GNM_CMD_CONTEXT (context),
			 _("No Workbook or Book streams found."));
		g_object_unref (G_OBJECT (ole));
		return;
	}

	excel_read_workbook (context, wbv, stream, &is_double_stream_file);
	g_object_unref (G_OBJECT (stream));

	excel_read_metadata (wb, ole, "\05SummaryInformation", context);
	excel_read_metadata (wb, ole, "\05DocumentSummaryInformation", context);

	/* See if there are any macros to keep around */
	stream = gsf_infile_child_by_name (ole, "\01CompObj");
	if (stream != NULL) {
		GsfInput *macros = gsf_infile_child_by_name (ole, "_VBA_PROJECT_CUR");
		if (macros != NULL) {
			g_object_set_data_full (G_OBJECT (wb), "MS_EXCEL_COMPOBJ",
				gsf_structured_blob_read (stream), g_object_unref);
			g_object_set_data_full (G_OBJECT (wb), "MS_EXCEL_MACROS",
				gsf_structured_blob_read (macros), g_object_unref);
			g_object_unref (G_OBJECT (macros));
		}
		g_object_unref (G_OBJECT (stream));
	}

	g_object_unref (G_OBJECT (ole));

	/* simple guess of format based on stream names */
	if (is_double_stream_file)
		workbook_set_saveinfo (wb, FILE_FL_AUTO,
			gnm_file_saver_for_id ("Gnumeric_Excel:excel_dsf"));
	else if (is_97)
		workbook_set_saveinfo (wb, FILE_FL_AUTO,
			gnm_file_saver_for_id ("Gnumeric_Excel:excel_biff8"));
	else
		workbook_set_saveinfo (wb, FILE_FL_AUTO,
			gnm_file_saver_for_id ("Gnumeric_Excel:excel_biff7"));
}

static void
excel_save (IOContext *context, WorkbookView const *wbv, GsfOutput *output,
	    gboolean biff7, gboolean biff8)
{
	Workbook *wb;
	GsfOutput *content;
	GsfOutfile *outfile;
	ExcelWriteState *ewb = NULL;
	GsfStructuredBlob *blob;

	io_progress_message (context, _("Preparing to save..."));
	io_progress_range_push (context, 0.0, 0.1);
	ewb = excel_write_state_new (context, wbv, biff7, biff8);
	io_progress_range_pop (context);
	if (ewb == NULL)
		return;

	wb = wb_view_workbook (wbv);
	outfile = gsf_outfile_msole_new (output);
	ewb->export_macros = (biff8 &&
		NULL != g_object_get_data (G_OBJECT (wb), "MS_EXCEL_MACROS"));

	io_progress_message (context, _("Saving file..."));
	io_progress_range_push (context, 0.1, 1.0);
	if (biff7)
		excel_write_v7 (ewb, outfile);
	if (biff8)
		excel_write_v8 (ewb, outfile);
	excel_write_state_free (ewb);
	io_progress_range_pop (context);

	blob = g_object_get_data (G_OBJECT (wb), "\05DocumentSummaryInformation");
	if (blob == NULL) {
		content = gsf_outfile_new_child (outfile,
			"\05DocumentSummaryInformation", FALSE);
		gsf_msole_metadata_write (content, TRUE, NULL);
		gsf_output_close (content);
		g_object_unref (G_OBJECT (content));
	} else
		gsf_structured_blob_write (blob, outfile);

	blob = g_object_get_data (G_OBJECT (wb), "\05SummaryInformation");
	if (blob == NULL) {
		content = gsf_outfile_new_child (outfile,
			"\05SummaryInformation", FALSE);
		gsf_msole_metadata_write (content, FALSE, NULL);
		gsf_output_close (content);
		g_object_unref (G_OBJECT (content));
	} else
		gsf_structured_blob_write (blob, outfile);

	/* restore the macros we loaded */
	blob = g_object_get_data (G_OBJECT (wb), "MS_EXCEL_COMPOBJ");
	if (blob != NULL)
		gsf_structured_blob_write (blob, outfile);
	blob = g_object_get_data (G_OBJECT (wb), "MS_EXCEL_MACROS");
	if (blob != NULL)
		gsf_structured_blob_write (blob, outfile);

	gsf_output_close (GSF_OUTPUT (outfile));
	g_object_unref (G_OBJECT (outfile));
}

void
excel_dsf_file_save (GnmFileSaver const *fs, IOContext *context,
		       WorkbookView const *wbv, GsfOutput *output)
{
	excel_save (context, wbv, output, TRUE, TRUE);
}
void
excel_biff8_file_save (GnmFileSaver const *fs, IOContext *context,
		       WorkbookView const *wbv, GsfOutput *output)
{
	excel_save (context, wbv, output, FALSE, TRUE);
}

void
excel_biff7_file_save (GnmFileSaver const *fs, IOContext *context,
		       WorkbookView const *wbv, GsfOutput *output)
{
	excel_save (context, wbv, output, TRUE, FALSE);
}


#include <formula-types.h>
void
plugin_init (void)
{
	excel_read_init ();
	excel_xml_read_init ();

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
				fprintf (stderr, "%s : flags in addition to XLM\n", name);
			if (fd->min_args != fd->max_args)
				fprintf (stderr, "%s : min != max\n", name);
			continue;
		}
		if (fd->min_args < 0)
			fprintf (stderr, "%s : min_args < 0\n", name);
		if (fd->max_args < 0)
			fprintf (stderr, "%s : min_args < 0\n", name);
		if (fd->min_args != fd->max_args) {
			if (0 == (fd->flags & XL_VARARG))
				fprintf (stderr, "%s : should be vararg\n", name);
			if (fd->flags & XL_FIXED)
				fprintf (stderr, "%s : should not be fixed\n", name);
		} else {
			if (fd->flags & XL_VARARG)
				fprintf (stderr, "%s : should not be vararg\n", name);
			if (0 == (fd->flags & XL_FIXED))
				fprintf (stderr, "%s : should be fixed\n", name);
		}
		if (fd->known_args != NULL &&
		    fd->num_known_args != strlen (fd->known_args))
			fprintf (stderr, "%s : num_expected_args inconsistent\n", name);
	}
}
#endif
}

/*
 * Cleanup allocations made by this plugin.
 * (Called right before we are unloaded.)
 */
void
plugin_cleanup (void)
{
	destroy_xl_font_widths ();
	excel_read_cleanup ();
	excel_xml_read_cleanup ();
}
