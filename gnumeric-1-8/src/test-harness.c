/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "libgnumeric.h"
#include "gnumeric-paths.h"
#include "gnm-plugin.h"
#include "cell.h"
#include "command-context.h"
#include "command-context-stderr.h"
#include "value.h"
#include "value-sheet.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "sheet-style.h"
#include "style.h"
#include "style-color.h"
#include <goffice/app/file.h>
#include <goffice/app/io-context.h>
#include <goffice/app/go-cmd-context.h>
#include <goffice/utils/go-file.h>
#include <gsf/gsf-utils.h>
#include <string.h>

#ifdef G_OS_WIN32
#define EXT_OPT(a,b,c,d,e,f,g) {a, b, c, d, NULL, f, g}
#else
#define EXT_OPT(a,b,c,d,e,f,g) {a, b, c, d, e, f, g}
#endif

static GOptionEntry
test_options[] = {
	EXT_OPT ("lib-dir", 'L', 0, G_OPTION_ARG_STRING, &gnumeric_lib_dir,
	  N_("Set the root library directory"), NULL),
	EXT_OPT ("data-dir", 'D', 0, G_OPTION_ARG_STRING, &gnumeric_data_dir,
	  N_("Adjust the root data directory"), NULL),

	{ NULL }
};

int
main (int argc, char const *argv [])
{
	ErrorInfo	*plugin_errs;
	int		 res = 0;
	GOCmdContext	*cc;
	GError		*error = NULL;
	GOptionContext	*ctx;

	argv = gnm_pre_parse_init (argc, argv);

#ifdef G_OS_WIN32
	test_options[1].arg = &gnumeric_lib_dir;
	test_options[2].arg = &gnumeric_data_dir;
#endif

	ctx = g_option_context_new ("");
	g_option_context_add_main_entries (ctx, test_options, GETTEXT_PACKAGE);
	g_option_context_parse (ctx, &argc, (gchar ***) &argv, &error);

	gnm_init (FALSE);

	cc = cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);

	{
		Workbook *wb = workbook_new_with_sheets (1);
		Sheet *sheet;
		GnmCell *c11, *c22, *c33;
		GOFileSaver *fs = NULL;
		WorkbookView *wbv;
		GnmStyle *style;

		sheet = workbook_sheets (wb)->data;
		style = gnm_style_new_default ();
		gnm_style_set_back_color (style, style_color_new_i8 (127, 127, 127));
		gnm_style_set_pattern (style, 1);
		sheet_style_set_pos (sheet, 0, 0, style);

		c11 = sheet_cell_fetch (sheet, 0, 0);
		gnm_cell_set_text (c11, "2");
		c22 = sheet_cell_fetch (sheet, 1, 1);
		gnm_cell_set_text (c22, "3");
		c33 = sheet_cell_fetch (sheet, 2, 2);
		gnm_cell_set_text (c33, "=A1*B2");
		workbook_recalc_all (wb);
		value_dump (c33->value);
		gnm_cell_set_text (c11, "3");
		cell_queue_recalc (c11);
		workbook_recalc (wb);
		value_dump (c33->value);

		fs = go_file_saver_for_id ("Gnumeric_XmlIO:xml_sax");
		wbv = workbook_view_new (wb);
		wb_view_save_as (wbv, fs, go_shell_arg_to_uri ("test.gnumeric"), cc);
		g_object_unref (wbv);

		g_object_unref (wb);
	}

	g_option_context_free (ctx);
	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return res;
}
