/**
 * boot.c: Lotus 123 support for Gnumeric
 *
 * Author:
 *    See: README
 *    Michael Meeks <mmeeks@gnu.org>
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "lotus.h"
#include "lotus-types.h"

#include <workbook-view.h>
#include <file.h>
#include <plugin.h>
#include <plugin-util.h>
#include <module-plugin-defs.h>

#include <gsf/gsf-input.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean lotus_file_probe (GnumFileOpener const *fo, GsfInput *input,
                           FileProbeLevel pl);
void     lotus_file_open (GnumFileOpener const *fo, IOContext *io_context,
                          WorkbookView *wb_view, GsfInput *input);


gboolean
lotus_file_probe (GnumFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	char const *header = NULL;
	if (!gsf_input_seek (input, 0, G_SEEK_SET))
		header = gsf_input_read (input, 4, NULL);
	gsf_input_seek (input, 0, G_SEEK_SET);
	return header != NULL &&
	       header[0] == (LOTUS_BOF & 0xff) &&
	       header[1] == ((LOTUS_BOF >> 8) & 0xff) &&
	       header[2] == (2 & 0xff) &&
	       header[3] == ((2 >> 8) & 0xff);
}

void
lotus_file_open (GnumFileOpener const *fo, IOContext *io_context,
                 WorkbookView *wb_view, GsfInput *input)
{
	Workbook *wb = wb_view_workbook (wb_view);

	lotus_read (io_context, wb, input);
}
