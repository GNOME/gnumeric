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
#include "gnome-xml/tree.h"
#include "gnome-xml/parser.h"
#include "color.h"
#include "sheet-object.h"
#include "style.h"

#include "ms-ole.h"
#include "ms-biff.h"
#include "ms-formula.h"
#include "ms-excel.h"
#include "ms-excel-biff.h"
#include "boot.h"

static gboolean
excel_probe (const char *filename)
{
	MS_OLE *f;
	int res;
	
	f = ms_ole_new (filename);

	res = f != NULL;

	ms_ole_destroy (f);

	return res;
}

static Workbook *
excel_load (const char *filename)
{
	Workbook *wb;
	MS_OLE *f;

	f = ms_ole_new (filename);
	if (!f)
		return NULL;

	wb = ms_excelReadWorkbook (f);
	ms_ole_destroy (f);

	return wb;
}

void
excel_init (void)
{
	char *desc = _("Microsoft(R) Excel file format");

	/* We register Excel format with a precendence of 100 */
	file_format_register_open (100, desc, excel_probe, excel_load);
}
