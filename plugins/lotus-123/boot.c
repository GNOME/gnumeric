/**
 * boot.c: Lotus 123 support for Gnumeric
 *
 * Author:
 *    See: README
 *    Michael Meeks <mmeeks@gnu.org>
 **/
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>
#include "lotus.h"
#include "lotus-types.h"

#include <file.h>
#include <workbook-view.h>
#include <plugin.h>
#include <workbook.h>
#include <plugin-util.h>
#include <module-plugin-defs.h>
#include <error-info.h>
#include <gutils.h>
#include <io-context.h>

#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean lotus_file_probe (GnmFileOpener const *fo, GsfInput *input,
                           FileProbeLevel pl);
void     lotus_file_open (GnmFileOpener const *fo, IOContext *io_context,
                          GODoc *doc, GsfInput *input);


gboolean
lotus_file_probe (GnmFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	char const *header = NULL;
	if (!gsf_input_seek (input, 0, G_SEEK_SET))
		header = gsf_input_read (input, 4, NULL);
	return header != NULL &&
	       header[0] == (LOTUS_BOF & 0xff) &&
	       header[1] == ((LOTUS_BOF >> 8) & 0xff) &&
	       header[2] == (2 & 0xff) &&
	       header[3] == ((2 >> 8) & 0xff);
}

void
lotus_file_open (GnmFileOpener const *fo, IOContext *io_context,
                 GODoc *doc, GsfInput *input)
{
	LotusWk1Read state;

	state.input	 = input;
	state.io_context = io_context;
	state.wb	 = WORKBOOK (doc);
	state.sheet	 = NULL;
	state.converter	 = g_iconv_open ("UTF-8", "ISO-8859-1");

	if (!lotus_wk1_read (&state))
		gnumeric_io_error_string (io_context,
			_("Error while reading lotus workbook."));

	gsf_iconv_close (state.converter);
}
