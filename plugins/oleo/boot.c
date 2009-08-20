/**
 * boot.c: Oleo support for Gnumeric
 *
 * Author:
 *    Robert Brady <rwb197@ecs.soton.ac.uk>
 *
 * (this file adapted from lotus-123/boot.c)
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "oleo.h"

#include <workbook-view.h>
#include <goffice/goffice.h>
#include <gnm-plugin.h>

GNM_PLUGIN_MODULE_HEADER;

void oleo_file_open (GOFileOpener const *fo, GOIOContext *io_context,
                     WorkbookView *wb_view, GsfInput *input);

void
oleo_file_open (GOFileOpener const *fo, GOIOContext *io_context,
                WorkbookView *wb_view, GsfInput *input)
{
	Workbook *wb = wb_view_get_workbook (wb_view);

	oleo_read (io_context, wb, input);
}
