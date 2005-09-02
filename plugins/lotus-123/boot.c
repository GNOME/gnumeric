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

#include <goffice/app/file.h>
#include <workbook-view.h>
#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>
#include <goffice/app/error-info.h>
#include <gutils.h>
#include <goffice/app/io-context.h>

#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-msole-utils.h>

GNM_PLUGIN_MODULE_HEADER;

gboolean lotus_file_probe (GOFileOpener const *fo, GsfInput *input,
                           FileProbeLevel pl);
void     lotus_file_open (GOFileOpener const *fo, IOContext *io_context,
                          WorkbookView *wb_view, GsfInput *input);


gboolean
lotus_file_probe (GOFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	char const *h = NULL;
	int len;
	LotusVersion version;

	if (!gsf_input_seek (input, 0, G_SEEK_SET))
		h = gsf_input_read (input, 6, NULL);

	if (h == NULL ||
	    GSF_LE_GET_GUINT16 (h + 0) != LOTUS_BOF)
		return FALSE;

	len = GSF_LE_GET_GUINT16 (h + 2);
	if (len < 2)
		return FALSE;

	version = GSF_LE_GET_GUINT16 (h + 4);
	switch (version) {
	case LOTUS_VERSION_ORIG_123:
	case LOTUS_VERSION_SYMPHONY:
		return len == 2;

	case LOTUS_VERSION_123V6:
	case LOTUS_VERSION_123SS98:
		return TRUE;

	default:
		return FALSE;
	}
}

void
lotus_file_open (GOFileOpener const *fo, IOContext *io_context,
                 WorkbookView *wb_view, GsfInput *input)
{
	LotusWk1Read state;

	state.input	 = input;
	state.io_context = io_context;
	state.wbv	 = wb_view;
	state.wb	 = wb_view_workbook (wb_view);
	state.sheet	 = NULL;

	/*
	 * "Lotus International Character Set" seems to be the same
	 * as CP850.  Information is sparse (beyond the acronym)
	 * in Google.
	 */
	state.converter =
		gsf_msole_iconv_open_for_import (850);
	if (state.converter == (GIConv)-1) {
		g_warning ("Unable to obtain proper chacterset converter.");
		state.converter	 = g_iconv_open ("UTF-8", "ISO-8859-1");
	}

	if (!lotus_read (&state))
		gnumeric_io_error_string (io_context,
			_("Error while reading lotus workbook."));

	gsf_iconv_close (state.converter);
}
