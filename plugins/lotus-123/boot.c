/**
 * boot.c: Lotus 123 support for Gnumeric
 *
 * Authors:
 *    See: README
 *    Michael Meeks <mmeeks@gnu.org>
 *    Morten Welinder (terra@gnome.org)
 **/
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "lotus.h"
#include "lotus-formula.h"
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
	case LOTUS_VERSION_SYMPHONY2:
		return len == 2;

	case LOTUS_VERSION_123V4: /* Barely and crudely handled.  */
	case LOTUS_VERSION_123V6:
	case LOTUS_VERSION_123V7:
	case LOTUS_VERSION_123SS98:
		return len >= 19;

	default:
		return FALSE;
	}
}

void
lotus_file_open (GOFileOpener const *fo, IOContext *io_context,
                 WorkbookView *wb_view, GsfInput *input)
{
	LotusState state;

	state.input	 = input;
	state.io_context = io_context;
	state.wbv	 = wb_view;
	state.wb	 = wb_view_get_workbook (wb_view);
	state.sheet	 = NULL;

	if (!lotus_read (&state))
		gnumeric_io_error_string (io_context,
			_("Error while reading lotus workbook."));
}



G_MODULE_EXPORT void
go_plugin_init (G_GNUC_UNUSED GOPlugin *plugin,
		G_GNUC_UNUSED GOCmdContext *cc)
{
	lmbcs_init ();
	lotus_formula_init ();
}

G_MODULE_EXPORT void
go_plugin_shutdown (G_GNUC_UNUSED GOPlugin *plugin,
		    G_GNUC_UNUSED GOCmdContext *cc)
{
	lotus_formula_shutdown ();
	lmbcs_shutdown ();
}
