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

#include <workbook.h>
#include <plugin.h>
#include <plugin-util.h>
#include <module-plugin-defs.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

void oleo_file_open (GnmFileOpener const *fo, IOContext *io_context,
                     GODoc *doc, GsfInput *input);

void
oleo_file_open (GnmFileOpener const *fo, IOContext *io_context,
                GODoc *doc, GsfInput *input)
{
	oleo_read (io_context, WORKBOOK (doc), input);
}
