/**
 * boot.c: Oleo support for Gnumeric
 *
 * Author:
 *    Robert Brady <rwb197@ecs.soton.ac.uk>
 *
 * (this file adapted from lotus-123/boot.c)
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
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "gnumeric-util.h"
#include "main.h"
#include "sheet.h"
#include "file.h"
#include "error-info.h"
#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"
#include "oleo.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

void oleo_file_open (GnumFileOpener const *fo, IOContext *io_context,
                     WorkbookView *wb_view, const char *filename);


void
oleo_file_open (GnumFileOpener const *fo, IOContext *io_context,
                WorkbookView *wb_view, const char *filename)
{
	Workbook *wb = wb_view_workbook (wb_view);

	oleo_read (io_context, wb, filename);
}
