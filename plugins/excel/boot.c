/**
 * boot.c: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 **/
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>

#include "file.h"
#include "libgnumeric.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

#include "excel.h"
#include "boot.h"
#include "ms-excel-util.h"
#include "ms-excel-read.h"

#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-msole.h>

#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-outfile.h>
#include <gsf/gsf-outfile-msole.h>

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
gint ms_excel_object_debug = 10;

MsExcelReadGbFn ms_excel_read_gb = NULL;

gboolean excel_file_probe (GnumFileOpener const *fo, GsfInput *input, FileProbeLevel pl);
void excel_file_open (GnumFileOpener const *fo, IOContext *context, WorkbookView *new_wb_view, GsfInput *input);
void excel97_file_save (GnumFileSaver const *fs, IOContext *context, WorkbookView *wb_view, const char *filename);
void excel95_file_save (GnumFileSaver const *fs, IOContext *context, WorkbookView *wb_view, const char *filename);
void plugin_cleanup (void);

gboolean
excel_file_probe (GnumFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	GsfInfile *ole;
	GsfInput *stream;
	gboolean res = FALSE;

	if (input == NULL)
		return FALSE;
	ole = gsf_infile_msole_new (input, NULL);
	if (ole == NULL)
		return FALSE;

	stream = gsf_infile_child_by_name (ole, "Workbook");
	if (stream == NULL)
		stream = gsf_infile_child_by_name (ole, "Book");

	if (stream != NULL) {
		g_object_unref (G_OBJECT (stream));
		res = TRUE;
	}
	g_object_unref (G_OBJECT (ole));

	return FALSE;
}

/*
 * excel_file_open
 * @context:   	Command context
 * @wb:    	    Workbook
 * @filename:  	File name
 * @user_data:  ignored
 *
 * Load en excel workbook.
 */
void
excel_file_open (GnumFileOpener const *fo, IOContext *context,
                 WorkbookView *new_wb_view, GsfInput *input)
{
	GsfInput *stream = NULL;
	GError   *err = NULL;
	GsfInfile *ole = gsf_infile_msole_new (input, &err);

	if (ole == NULL) {
		g_return_if_fail (err != NULL);
		gnumeric_io_error_read (context, err->message);
		g_error_free (err);
		return;
	}

	stream = gsf_infile_child_by_name (ole, "Workbook");
	if (stream == NULL)
		stream = gsf_infile_child_by_name (ole, "Book");

	if (stream == NULL) {
		gnumeric_io_error_read (context,
			 _("No Workbook or Book streams found."));
		g_object_unref (G_OBJECT (ole));
		return;
	}

	ms_excel_read_workbook (context, new_wb_view, stream);
#warning re-enable this when gsf handles doc metadata
#warning TODO we can now support pre-ole files ! but first find a way to id them.
#if 0
	if (!gnumeric_io_error_occurred (context)) {
		Workbook *wb = wb_view_workbook (new_wb_view);
		ms_summary_read (f, wb->summary_info);

		if (ms_excel_read_debug > 0)
			summary_info_dump (wb->summary_info);

		if (ms_excel_read_gb) {
			if (!ms_excel_read_gb (context, wb, f))
				g_warning ("Failed to read Basic scripts");
		}
	}
#endif

	g_object_unref (G_OBJECT (ole));
	g_object_unref (G_OBJECT (stream));
}

/*
 * Here's why the state which is carried from excel_check_write to
 * ms_excel_write_workbook is void *: The state is actually an
 * ExcelWorkbook * as defined in ms-excel-write.h. But we can't
 * import that definition here: There's a different definition of
 * ExcelWorkbook in ms-excel-read.h.
 */
static void
excel_save (IOContext *context, WorkbookView *wb_view, const char *filename,
            MsBiffVersion ver)
{
	GsfOutput *output;
	GsfOutfile *outfile;
	void *state = NULL;
	GError    *err;
	gint res;

	io_progress_message (context, _("Preparing for save..."));
	io_progress_range_push (context, 0.0, 0.1);
	res = ms_excel_check_write (context, &state, wb_view, ver);
	io_progress_range_pop (context);

	if (res != 0) {
		gnumeric_io_error_unknown (context);
		return;
	}

	output = gsf_output_stdio_new (filename, &err);
	if (output == NULL) {
		char *str = g_strdup_printf (_("Can't open '%s' : %s"),
			filename, err->message);
		gnumeric_io_error_save (context, str);
		ms_excel_write_free_state (state);
		g_error_free (err);
		g_free (str);
		return;
	}

	io_progress_message (context, _("Saving file..."));
	io_progress_range_push (context, 0.1, 1.0);
	outfile = gsf_outfile_msole_new (output);
	g_object_unref (G_OBJECT (output));
	ms_excel_write_workbook (context, outfile, state, ver);
	io_progress_range_pop (context);

#warning re-enable when gsf meta data generator is ready
#if 0
	Workbook *wb = wb_view_workbook (wb_view);
	ms_summary_write (f, wb->summary_info);
#endif
	gsf_output_close (GSF_OUTPUT (outfile));
	g_object_unref (G_OBJECT (outfile));
}

void
excel97_file_save (GnumFileSaver const *fs, IOContext *context,
                   WorkbookView *wb_view, const char *filename)
{
	excel_save (context, wb_view, filename, MS_BIFF_V8);
}

void
excel95_file_save (GnumFileSaver const *fs, IOContext *context,
                   WorkbookView *wb_view, const char *filename)
{
	excel_save (context, wb_view, filename, MS_BIFF_V7);
}


void
plugin_init (void)
{
	ms_excel_read_init ();
}

/*
 * Cleanup allocations made by this plugin.
 * (Called right before we are unloaded.)
 */
void
plugin_cleanup (void)
{
	destroy_xl_font_widths ();
	ms_excel_read_cleanup ();
}
