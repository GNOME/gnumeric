/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Gnumeric, the GNOME spreadsheet.
 *
 * Main file, startup code.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "libgnumeric.h"

#include "application.h"
#include "stf.h"
#include "plugin.h"
#include "format.h"
#include "command-context.h"
#include "command-context-stderr.h"
#include "workbook.h"
#include "sheet-object.h"
#include "number-match.h"
#include "expr-name.h"
#include "func.h"
#include "application.h"
#include "print-info.h"
#include "global-gnome-font.h"
#include "style.h"
#include "mstyle.h"
#include "style-color.h"
#include "str.h"
#include "dependent.h"
#include "sheet-autofill.h"
#include "xml-io.h"
#include "cell.h"
#include "value.h"
#include "expr.h"
#include "parse-util.h"
#include "rendered-value.h"
#include "gnumeric-gconf.h"
#include "plugin-service.h"
#include "mathfunc.h"
#include "hlink.h"
#include "gnumeric-paths.h"
#include <goffice/goffice.h>

#include <locale.h>
#include <glade/glade.h>

/* The debugging level */
int gnumeric_debugging = 0;
int dependency_debugging = 0;
int expression_sharing_debugging = 0;
int immediate_exit_flag = 0;
int print_debugging = 0;
gboolean initial_workbook_open_complete = FALSE;
char	   *x_geometry;
char const *gnumeric_lib_dir = GNUMERIC_LIBDIR;
char const *gnumeric_data_dir = GNUMERIC_DATADIR;
char const *gnumeric_icon_dir = GNUMERIC_ICONDIR;
char const *gnumeric_locale_dir = GNUMERIC_LOCALEDIR;

/**
 * gnm_pre_parse_init :
 * @gnumeric_binary : argv[0]
 *
 * Initialization to be done before cmd line arguments are handled.
 **/
void
gnm_pre_parse_init (char const* gnumeric_binary)
{
	g_set_prgname (gnumeric_binary);

	/* Make stdout line buffered - we only use it for debug info */
	setvbuf (stdout, NULL, _IOLBF, 0);

#ifdef G_OS_WIN32
{
	char *dir;
	dir = g_win32_get_package_installation_directory (NULL, NULL);
	gnumeric_data_dir = g_build_filename (dir,
		"share", "gnumeric", GNUMERIC_VERSION, NULL);
	gnumeric_lib_dir = g_build_filename (dir,
		"lib", "gnumeric", GNUMERIC_VERSION, NULL);
	gnumeric_icon_dir = g_build_filename (dir,
		"lib", "pixmaps", "gnumeric", NULL);
	gnumeric_locale_dir = g_build_filename (dir,
		"lib", "locale", NULL);
}
#endif

	bindtextdomain (GETTEXT_PACKAGE, gnumeric_locale_dir);
	textdomain (GETTEXT_PACKAGE);

	/* Force all of the locale segments to update from the environment.
	 * Unless we do this they will default to C
	 */
	setlocale (LC_ALL, "");
}

#if 0
static void
gnumeric_check_for_components (void)
{
	OAF_ServerInfoList *result;

	result = oaf_query ("repo_ids.has('IDL::1.0')", NULL, NULL);

	g_return_if_fail (result != NULL);

	CORBA_free (info_list);
}
#endif

extern void libgoffice_init (void);
/*
 * FIXME: We hardcode the GUI command context. Change once we are able
 * to tell whether we are in GUI or not.
 */
void
gnm_common_init (gboolean fast)
{
	plugin_services_init ();
	libgoffice_init ();

	g_object_new (GNM_APP_TYPE, NULL);
	mathfunc_init ();
	gnm_string_init ();
	mstyle_init ();
	gnm_conf_init (fast);
	value_init ();
	parse_util_init ();
	expr_init ();
	cell_init ();
	dependent_types_init ();
	rendered_value_init ();
	gnumeric_color_init ();
	style_init ();
	format_color_init ();
	format_match_init ();
	/* e_cursors_init (); */
	functions_init ();
	print_init ();
	autofill_init ();
	sheet_objects_init ();

	/* make sure that all hlink types are registered */
	gnm_hlink_cur_wb_get_type ();
	gnm_hlink_url_get_type ();
	gnm_hlink_email_get_type ();
	gnm_hlink_external_get_type ();

	/* The statically linked in file formats */
	xml_init ();
	stf_init ();

	glade_init ();
}

int
gnm_dump_func_defs (char const* filename, gboolean def_or_state)
{
	int retval;
	GnmCmdContext *cc = cmd_context_stderr_new ();

	plugins_init (cc);
	if ((retval = cmd_context_stderr_get_status (COMMAND_CONTEXT_STDERR (cc))) == 0)
		function_dump_defs (filename, def_or_state);

	return retval;
}

void
gnm_shutdown (void)
{
	gnm_app_release_pref_dialog ();
	gnm_app_clipboard_clear (TRUE);

	plugins_shutdown ();
	print_shutdown ();
	functions_shutdown ();
	/* e_cursors_shutdown (); */
	format_match_finish ();
	format_color_shutdown ();
	style_shutdown ();
	gnumeric_color_shutdown ();
	rendered_value_shutdown ();
	dependent_types_shutdown ();
	cell_shutdown ();
	expr_shutdown ();
	parse_util_shutdown ();
	value_shutdown ();
	gnm_conf_shutdown ();
	mstyle_shutdown ();
	gnm_string_shutdown ();
	libgoffice_shutdown ();
	plugin_services_shutdown ();
	g_object_unref (gnm_app_get_app ());
}
