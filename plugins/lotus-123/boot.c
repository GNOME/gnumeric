/**
 * boot.c: Lotus 123 support for Gnumeric
 *
 * Author:
 *    See: README
 *    Michael Meeks <mmeeks@gnu.org>
 **/
#include <config.h>
#include "lotus.h"
#include "lotus-types.h"

#include <gnumeric.h>
#include <workbook-view.h>
#include <file.h>
#include <plugin.h>
#include <plugin-util.h>
#include <module-plugin-defs.h>

#include <stdio.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean lotus_file_probe (GnumFileOpener const *fo, const gchar *file_name,
                           FileProbeLevel pl);
void     lotus_file_open (GnumFileOpener const *fo, IOContext *io_context,
                          WorkbookView *wb_view, const char *filename);


gboolean
lotus_file_probe (GnumFileOpener const *fo, const gchar *file_name, FileProbeLevel pl)
{
	unsigned char magic[4];
	FILE *f;
	gint rcount;

	f = fopen (file_name, "rb");
	if (f == NULL) {
		return FALSE;
	}
	rcount = fread (magic, 1, 4, f);
	(void) fclose (f);

	return rcount == 4 &&
	       magic[0] == (LOTUS_BOF & 0xff) &&
	       magic[1] == ((LOTUS_BOF >> 8) & 0xff) &&
	       magic[2] == (2 & 0xff) &&
	       magic[3] == ((2 >> 8) & 0xff);
}

void
lotus_file_open (GnumFileOpener const *fo, IOContext *io_context,
                 WorkbookView *wb_view, const char *file_name)
{
	Workbook *wb = wb_view_workbook (wb_view);

	lotus_read (io_context, wb, file_name);
}
