/**
 * lotus.c: Lotus 123 support for Gnumeric
 *
 * Author:
 *    See: README
 *    Michael Meeks <michael@imagiantor.com>
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

#include "lotus.h"

Workbook *
lotus_read (const char *filename)
{
	Workbook *wb;
	Sheet    *sheet;

	cell_deep_freeze_redraws ();
	
	wb = workbook_new ();

	sheet = sheet_new (wb, filename);
	workbook_attach_sheet (wb, sheet);

	if (wb)
		workbook_recalc (wb);
	cell_deep_thaw_redraws ();
}





