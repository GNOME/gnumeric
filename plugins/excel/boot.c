/**
 * ms-excel.c: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "main.h"
#include "sheet.h"
#include "file.h"

#include "excel.h"
#include "boot.h"

static gboolean
excel_probe (const char *filename)
{
	MS_OLE *f;
	int res;
	
	f = ms_ole_open (filename);

	res = f != NULL;

	ms_ole_destroy (f);

	return res;
}

static Workbook *
excel_load (const char *filename)
{
	Workbook *wb;
	MS_OLE *f;

	f = ms_ole_open (filename);
	if (!f)
		return NULL;

	wb = ms_excel_read_workbook (f);
	if (wb) {
		char *name = g_strconcat (filename, ".gnumeric", NULL);
		workbook_set_filename (wb, name);
		g_free(name);
	}

	ms_ole_destroy (f);

	return wb;
}


static int
excel_save (Workbook *wb, const char *filename, eBiff_version ver)
{
	MS_OLE *f;
	int ans;
	struct stat s;

	if ((stat (filename, &s) != -1)) {
		gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("Saving over old files disabled for safety"));
		return 0;
	}
	
	f = ms_ole_create (filename);

	if (!f) {
		char *str = g_strdup_printf ("%s %s",
					     _("Can't open"),
					     filename);
		gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR, str);

		g_free (str);
		return 0;
	}

	ans = ms_excel_write_workbook (f, wb, ver);

	ms_ole_destroy (f);
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
	char *descr2 = _("Excel(R) 98 file format");
	char *descr3 = _("Excel(R) 95 file format");

	/* We register Excel format with a precendence of 100 */
	file_format_register_open (100, descr, excel_probe, excel_load);
	if (gnumeric_debugging > 0) {
		file_format_register_save (".xls", descr2, excel_save_98);
		file_format_register_save (".xls", descr3, excel_save_95);
	}
}
