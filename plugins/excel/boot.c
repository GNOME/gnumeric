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
#include "workbook.h"
#include "command-context.h"

#include "excel.h"
#include "ms-summary.h"
#include "boot.h"

extern int ms_excel_read_debug;
MsExcelReadGbFn ms_excel_read_gb = NULL;

static int
excel_save_95 (CommandContext *context, Workbook *wb, const char *filename);

static gboolean
excel_probe (const char *filename)
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
 * @context   command context
 * @wb        workbook
 * @filename  file name
 *
 * Load en excel workbook.
 * Returns 0 on success, -1 on failure.
 */
static int
excel_load (CommandContext *context, Workbook *wb, const char *filename)
{
	MsOleErr  ole_error;
	MsOle	 *f;
	int      result;

	ole_error = ms_ole_open (&f, filename);
	if (ole_error != MS_OLE_ERR_OK) {
		ms_ole_destroy (&f);
		/* FIXME : We need a more detailed message from
		 * ole_open */
		gnumeric_error_read (context, "");
		return -1;
	}

	result = ms_excel_read_workbook (context, wb, f);
	if (result == 0) {
		ms_summary_read (f, wb->summary_info);

		if (ms_excel_read_debug > 0)
			summary_info_dump (wb->summary_info);

		if (ms_excel_read_gb) {
			if (!ms_excel_read_gb (context, wb, f))
				g_warning ("Failed to read Basic scripts");
		}

		workbook_set_saveinfo (wb, filename, FILE_FL_MANUAL,
				       excel_save_95);
	}

	ms_ole_destroy (&f);

	return result;
}

/*
 * Here's why the state which is carried from excel_check_write to
 * ms_excel_write_workbook is void *: The state is actually an
 * ExcelWorksheet * as defined in ms-excel-write.h. But we can't
 * import that definition here: There's a different definition of
 * ExcelWorksheet in ms-excel-read.h.
 */
static int
excel_save (CommandContext *context, Workbook *wb, const char *filename,
	    MsBiffVersion ver)
{
	MsOle *f;
	int ans;
	struct stat s;
	MsOleErr result;
	void *state = NULL;
	
	if ((stat (filename, &s) != -1)) {
		gnumeric_error_save
			(context,
			 _("Saving over old files disabled for safety"));
		return -1;
	}

	if (ms_excel_check_write (context, &state, wb, ver) != 0)
		return -1;		

	result = ms_ole_create (&f, filename);

	if (result != MS_OLE_ERR_OK) {
		char *str = g_strdup_printf ("%s %s",
					     _("Can't open"),
					     filename);
		gnumeric_error_save (context, str);

		ms_ole_destroy (&f);
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
excel_save_98 (CommandContext *context, Workbook *wb, const char *filename)
{
	return excel_save (context, wb, filename, MS_BIFF_V8);
}

static int
excel_save_95 (CommandContext *context, Workbook *wb, const char *filename)
{
	return excel_save (context, wb, filename, MS_BIFF_V7);
}

void
excel_init (void)
{
	/* We register Excel format with a precendence of 100 */
	file_format_register_open (100, 
				   _("Microsoft(R) Excel file format"),
				   &excel_probe, &excel_load);
	if (gnumeric_debugging > 0)
		file_format_register_save (".xls", 
					   _("Excel(R) 97 file format"),
					   FILE_FL_MANUAL, &excel_save_98);

	file_format_register_save (".xls", 
				   _("Excel(R) 95 file format"),
				   FILE_FL_MANUAL, &excel_save_95);
}

void
excel_shutdown (void)
{
}
