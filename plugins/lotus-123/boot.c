/**
 * boot.c: Lotus 123 support for Gnumeric
 *
 * Author:
 *    See: README
 *    Michael Meeks <mmeeks@gnu.org>
 **/
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "lotus.h"
#include "lotus-types.h"

#include <workbook-view.h>
#include <plugin.h>
#include <plugin-util.h>
#include <module-plugin-defs.h>
#include <error-info.h>
#include <gutils.h>

#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>

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
	LotusWk1Read state;

	state.input	 = input;
	state.io_context = io_context;
	state.wbv	 = wb_view;
	state.wb	 = wb_view_workbook (wb_view);
	state.sheet	 = NULL;
	state.converter	 = g_iconv_open ("UTF-8", "ISO-8859-1");

	if (!lotus_wk1_read (&state))
		gnumeric_io_error_string (io_context,
			_("Error while reading lotus workbook."));

	gsf_iconv_close (state.converter);
}
