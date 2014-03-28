/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * main-application.c: Main entry point for the Gnumeric application
 *
 * Author:
 *   Jon Kåre Hellan <hellan@acm.org>
 *   Morten Welinder <terra@gnome.org>
 *   Jody Goldberg <jody@gnome.org>
 *
 * Copyright (C) 2002-2004, Jon Kåre Hellan
 */

#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "libgnumeric.h"
#ifdef G_OS_WIN32
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <io.h>
#endif

#include "command-context.h"
#include <goffice/goffice.h>
#include "io-context-gtk.h"
/* TODO: Get rid of this one */
#include "command-context-stderr.h"
#include "wbc-gtk-impl.h"
#include "workbook-view.h"
#include "workbook.h"
#include "gui-file.h"
#include "gnumeric-conf.h"
#include "gnumeric-paths.h"
#include "session.h"
#include "sheet.h"
#include "gutils.h"
#include "gnm-plugin.h"
#include "application.h"
#include "func.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <locale.h>

static gboolean split_funcdocs = FALSE;
static gboolean immediate_exit_flag = FALSE;
static gboolean gnumeric_no_splash = FALSE;
static gboolean gnumeric_no_warnings = FALSE;
static gchar  *func_def_file = NULL;
static gchar  *func_state_file = NULL;
static gchar  *ext_refs_file = NULL;
static gchar  *geometry = NULL;
static gchar **startup_files;

static const GOptionEntry gnumeric_options [] = {
	/*********************************
	 * Public Variables */
	{ "geometry", 'g', 0, G_OPTION_ARG_STRING, &geometry,
		N_("Specify the size and location of the initial window"),
		N_("WIDTHxHEIGHT+XOFF+YOFF")
	},
	{ "no-splash", 0, 0, G_OPTION_ARG_NONE, &gnumeric_no_splash,
		N_("Don't show splash screen"), NULL },
	{ "no-warnings", 0, 0, G_OPTION_ARG_NONE, &gnumeric_no_warnings,
		N_("Don't display warning dialogs when importing"),
		NULL
	},

	/*********************************
	 * Hidden Actions */
	{
		"dump-func-defs", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_FILENAME, &func_def_file,
		N_("Dumps the function definitions"),
		N_("FILE")
	},
	{
		"dump-func-state", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_FILENAME, &func_state_file,
		N_("Dumps the function definitions"),
		N_("FILE")
	},
	{
		"ext-refs-file", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_FILENAME, &ext_refs_file,
		N_("Dumps web page for function help"),
		N_("FILE")
	},
	{
		"split-func", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &split_funcdocs,
		N_("Generate new help and po files"),
		NULL
	},
	{
		"quit", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &immediate_exit_flag,
		N_("Exit immediately after loading the selected books"),
		NULL
	},
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &startup_files, NULL, NULL },
	{ NULL }
};


static void
handle_paint_events (void)
{
	/* FIXME: we need to mask input events correctly here */
	/* Show something coherent */
	while (gtk_events_pending () && !initial_workbook_open_complete)
		gtk_main_iteration_do (FALSE);
}

static GObject *program = NULL;

static void
gnumeric_arg_shutdown (void)
{
	if (program) {
		g_object_unref (program);
		program = NULL;
	}
}

static void
gnumeric_arg_parse (int argc, char **argv)
{
	GOptionContext *ocontext;
	int i;
	gboolean funcdump = FALSE;
	GError *error = NULL;

	/* no need to init gtk when dumping function info */
	for (i = 0 ; argv[i] ; i++)
		if (0 == strncmp ("--dump-func", argv[i], 11)) {
			funcdump = TRUE;
			break;
		}

	ocontext = g_option_context_new (_("[FILE ...]"));
	g_option_context_add_main_entries (ocontext, gnumeric_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());

#if defined(G_OS_WIN32)
	/* we have already translated to utf8, do not do it again.
	 * http://bugzilla.gnome.org/show_bug.cgi?id=361321 */
	g_option_context_set_delocalize   (ocontext, FALSE);
#endif

	if (!funcdump)
		g_option_context_add_group (ocontext, gtk_get_option_group (TRUE));
	g_option_context_parse (ocontext, &argc, &argv, &error);

	if (ocontext)
		g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, argv[0]);
		g_error_free (error);
		exit (1);
	}

	if (!funcdump) {
		gtk_init (&argc, &argv);
	}
}

/*
 * WARNING WARNING WARNING
 * This does not belong here
 * but it is expedient for now to get things to compile
 */
#warning "REMOVE REMOVE REMOVE"
static void
store_plugin_state (void)
{
	GSList *active_plugins = go_plugins_get_active_plugins ();
	gnm_conf_set_plugins_active (active_plugins);
	g_slist_free (active_plugins);
}

static gboolean
cb_kill_wbcg (WBCGtk *wbcg)
{
	gboolean still_open = wbc_gtk_close (wbcg);
	g_assert (!still_open);
	return FALSE;
}

static gboolean
pathetic_qt_workaround (void)
{
	/*
	 * When using with the Qt theme, the qt library will be initialized
	 * somewhere around the time the first widget is created or maybe
	 * realized.  That code literally does
	 *
	 *        setlocale( LC_NUMERIC, "C" );	// make sprintf()/scanf() work
	 *
	 * I am not kidding.  It seems like we can fix this by re-setting the
	 * proper locale when the gui comes up.
	 *
	 * See bug 512752, for example.
	 */
	setlocale (LC_ALL, "");
	return FALSE;
}


static void
cb_workbook_removed (void)
{
	if (gnm_app_workbook_list () == NULL) {
		gtk_main_quit ();
	}
}

int
main (int argc, char const **argv)
{
	gboolean opened_workbook = FALSE;
	gboolean with_gui;
	GOIOContext *ioc;
	WorkbookView *wbv;
	GSList *wbcgs_to_kill = NULL;
	GOCmdContext *cc;

#ifdef G_OS_WIN32
	gboolean has_console;
#endif

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

	/*
	 * Attempt to disable Ubuntu's funky, non-working scroll
	 * bars.  This needs to be done before gtk starts loading
	 * modules.  Note: the following call will not replace
	 * an existing setting, so you can run with =1 if you like.
	 */
	g_setenv ("LIBOVERLAY_SCROLLBAR", "0", FALSE);

#ifdef G_OS_WIN32
	has_console = FALSE;
	{
		typedef BOOL (CALLBACK* LPFNATTACHCONSOLE)(DWORD);
		LPFNATTACHCONSOLE MyAttachConsole;
		HMODULE hmod;

		if ((hmod = GetModuleHandle("kernel32.dll"))) {
			MyAttachConsole = (LPFNATTACHCONSOLE) GetProcAddress(hmod, "AttachConsole");
			if (MyAttachConsole && MyAttachConsole(ATTACH_PARENT_PROCESS)) {
				freopen("CONOUT$", "w", stdout);
				freopen("CONOUT$", "w", stderr);
				dup2(fileno(stdout), 1);
				dup2(fileno(stderr), 2);
				has_console = TRUE;
			}
		}
	}
#endif

	gnumeric_arg_parse (argc, (char **)argv);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	bind_textdomain_codeset (GETTEXT_PACKAGE "-functions", "UTF-8");

	with_gui = !func_def_file && !func_state_file && !split_funcdocs;

	if (with_gui) {
		gnm_session_init (argv[0]);
	}

	gnm_init ();

	/* These are useful for the build process only.  */
	if (func_state_file)
		return gnm_dump_func_defs (func_state_file, 0);
	if (func_def_file)
		return gnm_dump_func_defs (func_def_file, 1);
	if (split_funcdocs)
		return gnm_dump_func_defs (NULL, 2);
	if (ext_refs_file)
		return gnm_dump_func_defs (ext_refs_file, 4);

	if (with_gui) {
		go_component_set_default_command_context (cc = cmd_context_stderr_new ());
		g_object_unref (cc);
		cc = g_object_new (GNM_TYPE_IO_CONTEXT_GTK,
				   "show-splash", !gnumeric_no_splash,
				   "show-warnings", !gnumeric_no_warnings,
				   NULL);
		ioc = GO_IO_CONTEXT (g_object_ref (cc));
		handle_paint_events ();
		pathetic_qt_workaround ();
	} else {
		/* TODO: Make this inconsistency go away */
		cc = cmd_context_stderr_new ();
		ioc = go_io_context_new (cc);
		go_component_set_default_command_context (cc);
	}

	/* Keep in sync with .desktop file */
	g_set_application_name (_("Gnumeric Spreadsheet"));
	gnm_plugins_init (GO_CMD_CONTEXT (ioc));

	if (startup_files) {
		int i;

		for (i = 0; startup_files [i]; i++)
			;

		go_io_context_set_num_files (ioc, i);
		for (i = 0;
		     startup_files [i] && !initial_workbook_open_complete;
		     i++) {
			char *uri = go_shell_arg_to_uri (startup_files[i]);

			if (uri == NULL) {
				g_warning ("Ignoring invalid URI.");
				continue;
			}

			go_io_context_processing_file (ioc, uri);
			wbv = workbook_view_new_from_uri (uri, NULL, ioc, NULL);
			g_free (uri);

			if (go_io_error_occurred (ioc) ||
			    go_io_warning_occurred (ioc)) {
				go_io_error_display (ioc);
				go_io_error_clear (ioc);
			}
			if (wbv != NULL) {
				WBCGtk *wbcg;

				workbook_update_history (wb_view_get_workbook (wbv), GNM_FILE_SAVE_AS_STYLE_SAVE);

				wbcg = wbc_gtk_new (wbv, NULL, NULL, geometry);
				geometry = NULL;
				sheet_update (wb_view_cur_sheet	(wbv));
				opened_workbook = TRUE;
				gnm_io_context_gtk_set_transient_for (GNM_IO_CONTEXT_GTK (ioc),
						       wbcg_toplevel (wbcg));
				if (immediate_exit_flag)
					wbcgs_to_kill = g_slist_prepend (wbcgs_to_kill,
									 wbcg);
			}
			/* cheesy attempt to keep the ui from freezing during
			   load */
			handle_paint_events ();
			if (gnm_io_context_gtk_get_interrupted (GNM_IO_CONTEXT_GTK (ioc)))
				break; /* Don't load any more workbooks */
		}
	}

	g_object_unref (cc);
	cc = NULL;

	/* FIXME: Maybe we should quit here if we were asked to open
	   files and failed to do so. */

	/* If we were intentionally short circuited exit now */
	if (!initial_workbook_open_complete) {
		initial_workbook_open_complete = TRUE;
		if (!opened_workbook) {
			gint n_of_sheets = gnm_conf_get_core_workbook_n_sheet ();
			wbc_gtk_new (NULL,
				workbook_new_with_sheets (n_of_sheets),
				NULL, geometry);
		}

		if (immediate_exit_flag) {
			GSList *l;
			for (l = wbcgs_to_kill; l; l = l->next)
				g_idle_add ((GSourceFunc)cb_kill_wbcg, l->data);
		}

		g_signal_connect (gnm_app_get_app (),
				  "workbook_removed",
				  G_CALLBACK (cb_workbook_removed),
				  NULL);

		gnm_io_context_gtk_discharge_splash (GNM_IO_CONTEXT_GTK (ioc));
		g_object_unref (ioc);

		g_idle_add ((GSourceFunc)pathetic_qt_workaround, NULL);
		gtk_main ();
	} else {
		g_object_unref (ioc);
		g_slist_foreach (wbcgs_to_kill, (GFunc)cb_kill_wbcg, NULL);
	}

	g_slist_free (wbcgs_to_kill);
	gnumeric_arg_shutdown ();
	store_plugin_state ();
	gnm_shutdown ();

#if defined(G_OS_WIN32)
	if (has_console) {
		close(1);
		close(2);
		FreeConsole();
	}
#endif

	gnm_pre_parse_shutdown ();
	go_component_set_default_command_context (NULL);

	/*
	 * This helps finding leaks.  We might want it in developent
	 * only.
	 */
	if (with_gui && gnm_debug_flag ("close-displays")) {
		GSList *displays;

		gdk_flush();
		while (g_main_context_iteration (NULL, FALSE))
			;/* nothing */

		displays = gdk_display_manager_list_displays
			(gdk_display_manager_get ());
		g_slist_foreach (displays, (GFunc)gdk_display_close, NULL);
		g_slist_free (displays);
	}

	return 0;
}
