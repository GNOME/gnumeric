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
#include <goffice/app/module-plugin-defs.h>
#include <goffice/app/error-info.h>
#include <gutils.h>
#include <io-context.h>

#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean lotus_file_probe (GnmFileOpener const *fo, GsfInput *input,
                           FileProbeLevel pl);
void     lotus_file_open (GnmFileOpener const *fo, IOContext *io_context,
                          WorkbookView *wb_view, GsfInput *input);


gboolean
lotus_file_probe (GnmFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	char const *h = NULL;
	if (!gsf_input_seek (input, 0, G_SEEK_SET))
		h = gsf_input_read (input, 6, NULL);

	if (h == NULL ||
	    GSF_LE_GET_GUINT16 (h+0) != LOTUS_BOF)
		return FALSE;

	/* wk1 and wks */
	if (GSF_LE_GET_GUINT16 (h+2) == 2 &&
	    (GSF_LE_GET_GUINT8 (h+4) == 4 || GSF_LE_GET_GUINT8 (h+4) == 6) &&
	    GSF_LE_GET_GUINT8 (h+5) == 4)
		return TRUE;
	/* 123 */
	if (GSF_LE_GET_GUINT8 (h+3) == 0 &&
	    GSF_LE_GET_GUINT8 (h+4) == 3 &&
	    GSF_LE_GET_GUINT8 (h+5) == 0x10)
		return TRUE;
	return FALSE;
}

void
lotus_file_open (GnmFileOpener const *fo, IOContext *io_context,
                 WorkbookView *wb_view, GsfInput *input)
{
	LotusWk1Read state;

	state.input	 = input;
	state.io_context = io_context;
	state.wbv	 = wb_view;
	state.wb	 = wb_view_workbook (wb_view);
	state.sheet	 = NULL;
	state.converter	 = g_iconv_open ("UTF-8", "ISO-8859-1");

	if (!lotus_read (&state))
		gnumeric_io_error_string (io_context,
			_("Error while reading lotus workbook."));

	gsf_iconv_close (state.converter);
}
