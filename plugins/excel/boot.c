/**
 * boot.c: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 1998, 1999 Michael Meeks
 **/
#include <stdio.h>
#include <sys/stat.h>
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "file.h"
#include "main.h"

#include "excel.h"
#include "ms-summary.h"
#include "boot.h"

extern int ms_excel_read_debug;

static gboolean
excel_probe (const char *filename)
{
	MsOle    *f;
	MsOleErr  result;
	
	result = ms_ole_open (&f, filename);
	ms_ole_destroy (&f);

	return result == MS_OLE_ERR_OK;
}

static gboolean
excel_load (Workbook *wb, const char *filename)
{
	MsOle *f;
	gboolean ret;
	MsOleErr  result;
	
	result = ms_ole_open (&f, filename);
	if (result != MS_OLE_ERR_OK) {
		ms_ole_destroy (&f);
		return FALSE;
	}

	printf ("Opening '%s' ", filename);
	ret = ms_excel_read_workbook (wb, f);
	if (ret) {
		char *name = g_strconcat (filename, ".gnumeric", NULL);
		ms_summary_read (f, wb->summary_info);

		if (ms_excel_read_debug > 0)
			summary_info_dump (wb->summary_info);

		workbook_set_filename (wb, name);
		g_free(name);
	}

	ms_ole_destroy (&f);

	return ret;
}


static int
excel_save (Workbook *wb, const char *filename, eBiff_version ver)
{
	MsOle *f;
	int ans;
	struct stat s;
	MsOleErr result;

	if ((stat (filename, &s) != -1)) {
		gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("Saving over old files disabled for safety"));
		return 0;
	}
	
	result = ms_ole_create (&f, filename);

	if (result != MS_OLE_ERR_OK) {
		char *str = g_strdup_printf ("%s %s",
					     _("Can't open"),
					     filename);
		gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR, str);

		ms_ole_destroy (&f);
		g_free (str);
		return 0;
	}

	ans = ms_excel_write_workbook (f, wb, ver);

        ms_summary_write (f, wb->summary_info);

	ms_ole_destroy (&f);

	if (ans)
		printf ("Written successfully\n");
	else
		printf ("Error whilst writing\n");

	return ans;
}

static int
excel_save_98 (Workbook *wb, const char *filename)
{
	return excel_save (wb, filename, eBiffV8);
}

static int
excel_save_95 (Workbook *wb, const char *filename)
{
	return excel_save (wb, filename, eBiffV7);
}

void
excel_init (void)
{
	char *descr  = _("Microsoft(R) Excel file format");
	char *descr2 = _("Excel(R) 97 file format");
	char *descr3 = _("Excel(R) 95 file format");

	/* We register Excel format with a precendence of 100 */
	file_format_register_open (100, descr, excel_probe, excel_load);
	if (gnumeric_debugging > 0)
		file_format_register_save (".xls", descr2, excel_save_98);
	file_format_register_save (".xls", descr3, excel_save_95);
}

void
excel_shutdown (void)
{
}
