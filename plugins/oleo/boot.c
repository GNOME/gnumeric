/**
 * boot.c: Oleo support for Gnumeric
 *
 * Author:
 *    Robert Brady <rwb197@ecs.soton.ac.uk>
 *
 * (this file adapted from lotus-123/boot.c)
 **/
#include <config.h>
#include "oleo.h"

#include <workbook-view.h>
#include <plugin.h>
#include <plugin-util.h>
#include <module-plugin-defs.h>

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
