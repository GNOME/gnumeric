/**
 * boot.c: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 **/
#include <stdio.h>
#include <sys/stat.h>

#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "file.h"
#include "main.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "plugin-util.h"

#include "excel.h"
#include "ms-summary.h"
#include "boot.h"

extern int ms_excel_read_debug;
MsExcelReadGbFn ms_excel_read_gb = NULL;

static FileOpenerId excel_opener_id;
static FileSaverId excel95_saver_id, excel98_saver_id;

static int
excel_save_95 (IOContext *context, WorkbookView *wb_view,
               const char *filename, gpointer user_data);

static gboolean
excel_probe (const char *filename, gpointer user_data)
{
	MsOle    *file;

	if (ms_ole_open (&file, filename) == MS_OLE_ERR_OK) {
		MsOleErr     result;
		MsOleStream *stream;

		result = ms_ole_stream_open (&stream, file, "/", "workbook", 'r');
		ms_ole_stream_close (&stream);
		if (result == MS_OLE_ERR_OK) {
			ms_ole_destroy (&file);
			return TRUE;
		}

		result = ms_ole_stream_open (&stream, file, "/", "book", 'r');
		ms_ole_stream_close (&stream);
		ms_ole_destroy (&file);
		if (result == MS_OLE_ERR_OK)
			return TRUE;
	}
	return FALSE;
}

/*
 * excel_load
 * @context:   	Command context
 * @wb:    	    Workbook
 * @filename:  	File name
 * @user_data:  ignored
 *
 * Load en excel workbook.
 * Returns 0 on success, -1 on failure.
 */
static int
excel_load (IOContext *context, WorkbookView *new_wb_view,
            const char *filename, gpointer user_data)
{
	MsOleErr  ole_error;
	MsOle	 *f;
	int      result;

	ole_error = ms_ole_open (&f, filename);
	if (ole_error != MS_OLE_ERR_OK) {
		ms_ole_destroy (&f);
		/* FIXME : We need a more detailed message from
		 * ole_open */
		gnumeric_io_error_read (context, "");
		return -1;
	}

	puts (filename);
	result = ms_excel_read_workbook (context, new_wb_view, f);
	if (result == 0) {
		Workbook *wb = wb_view_workbook (new_wb_view);
		ms_summary_read (f, wb->summary_info);

		if (ms_excel_read_debug > 0)
			summary_info_dump (wb->summary_info);

		if (ms_excel_read_gb) {
			if (!ms_excel_read_gb (context, wb, f))
				g_warning ("Failed to read Basic scripts");
		}

		workbook_set_saveinfo (wb, filename, FILE_FL_MANUAL, excel95_saver_id);
	}

	ms_ole_destroy (&f);

	return result;
}

/*
 * Here's why the state which is carried from excel_check_write to
 * ms_excel_write_workbook is void *: The state is actually an
 * ExcelWorkbook * as defined in ms-excel-write.h. But we can't
 * import that definition here: There's a different definition of
 * ExcelWorkbook in ms-excel-read.h.
 */
static int
excel_save (IOContext *context, WorkbookView *wb_view, const char *filename,
	    MsBiffVersion ver)
{
	Workbook *wb = wb_view_workbook (wb_view);
	MsOle *f;
	int ans;
	struct stat s;
	MsOleErr result;
	void *state = NULL;

	if ((stat (filename, &s) != -1)) {
		gnumeric_io_error_save (context,
			 _("Saving over old files disabled for safety"));
		return -1;
	}

	if (ms_excel_check_write (context, &state, wb_view, ver) != 0)
		return -1;

	result = ms_ole_create (&f, filename);

	if (result != MS_OLE_ERR_OK) {
		char *str = g_strdup_printf ("%s %s",
					     _("Can't open"),
					     filename);
		gnumeric_io_error_save (context, str);

		ms_ole_destroy (&f);
		ms_excel_write_free_state (state);
		g_free (str);
		return -1;
	}

	ans = ms_excel_write_workbook (context, f, state, ver);

        ms_summary_write (f, wb->summary_info);

	ms_ole_destroy (&f);

	if (ans == 0)
		printf ("Written successfully\n");
	else
		printf ("Error whilst writing\n");

	return ans;
}

static int
excel_save_98 (IOContext *context, WorkbookView *wb_view,
               const char *filename, gpointer user_data)
{
	return excel_save (context, wb_view, filename, MS_BIFF_V8);
}

static int
excel_save_95 (IOContext *context, WorkbookView *wb_view,
               const char *filename, gpointer user_data)
{
	return excel_save (context, wb_view, filename, MS_BIFF_V7);
}

void
excel_init (void)
{
	/* We register Excel format with a precendence of 100 */
	excel_opener_id = file_format_register_open (
	                  100, _("Microsoft(R) Excel file format"),
	                  &excel_probe, &excel_load, NULL);
	if (gnumeric_debugging > 0) {
		excel98_saver_id = file_format_register_save (
		                   ".xls", _("Excel(R) 97 file format"),
		                   FILE_FL_MANUAL, &excel_save_98, NULL);
	}
	excel95_saver_id = file_format_register_save (
	                   ".xls", _("Excel(R) 95 file format"),
	                   FILE_FL_MANUAL, &excel_save_95, NULL);
}

void
excel_shutdown (void)
{
}
