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
#define POPT_STATIC
#define _WIN32_WINNT 0x0501
#include <windows.h>
#endif
#include <popt.h>

#include "command-context.h"
#include <goffice/app/io-context.h>
#include "io-context-gtk.h"
/* TODO: Get rid of this one */
#include "command-context-stderr.h"
#include "workbook-control-gui.h"
#include "workbook-control-gui-priv.h"
#include "workbook-view.h"
#include <goffice/app/go-plugin.h>
#include "workbook.h"
#include "gui-file.h"
#include "gnumeric-gconf.h"
#include "gnumeric-paths.h"
#include "session.h"
#include "sheet.h"
#include "gnm-plugin.h"

#include <gtk/gtkmain.h>
#include <glib/gstdio.h>
#include <goffice/utils/go-file.h>
#include <goffice/app/go-cmd-context.h>

#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>

#ifdef WITH_GNOME
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-ui-main.h>
#endif

static int gnumeric_show_version = FALSE;
static int split_funcdocs = FALSE;
static char *func_def_file = NULL;
static char *func_state_file = NULL;

int gnumeric_no_splash = FALSE;
int gnumeric_no_warnings = FALSE;

static struct poptOption
gnumeric_popt_options[] = {
	{ "version", 'v', POPT_ARG_NONE, &gnumeric_show_version, 0,
	  N_("Display Gnumeric's version"), NULL  },
	{ "lib-dir", 'L', POPT_ARG_STRING, &gnumeric_lib_dir, 0,
	  N_("Set the root library directory"), NULL  },
	{ "data-dir", 'D', POPT_ARG_STRING, &gnumeric_data_dir, 0,
	  N_("Adjust the root data directory"), NULL  },

	{ "dump-func-defs", '\0', POPT_ARG_STRING, &func_def_file, 0,
	  N_("Dumps the function definitions"),   N_("FILE") },
	{ "dump-func-state", '\0', POPT_ARG_STRING, &func_state_file, 0,
	  N_("Dumps the function definitions"),   N_("FILE") },
	{ "split-func", '\0', POPT_ARG_NONE, &split_funcdocs, 0,
	  N_("Generate new help and po files"),   NULL },

	{ "debug", '\0', POPT_ARG_INT, &gnumeric_debugging, 0,
	  N_("Enables some debugging functions"), N_("LEVEL") },

	{ "debug-deps", '\0', POPT_ARG_INT, &dependency_debugging, 0,
	  N_("Enables some dependency related debugging functions"), N_("LEVEL") },
	{ "debug-share", '\0', POPT_ARG_INT, &expression_sharing_debugging, 0,
	  N_("Enables some debugging functions for expression sharing"), N_("LEVEL") },
	{ "debug-print", '\0', POPT_ARG_INT, &print_debugging, 0,
	  N_("Enables some print debugging behavior"), N_("LEVEL") },

	{ "geometry", 'g', POPT_ARG_STRING, &x_geometry, 0,
	  N_("Specify the size and location of the initial window"), N_("WIDTHxHEIGHT+XOFF+YOFF")
	},

	{ "no-splash", '\0', POPT_ARG_NONE, &gnumeric_no_splash, 0,
	  N_("Don't show splash screen"), NULL },
	{ "no-warnings", '\0', POPT_ARG_NONE, &gnumeric_no_warnings, 0,
	  N_("Don't display warning dialogs when importing"), NULL },

	{ "quit", '\0', POPT_ARG_NONE, &immediate_exit_flag, 0,
	  N_("Exit immediately after loading the selected books (useful for testing)."), NULL },

	POPT_AUTOHELP
	POPT_TABLEEND
};

static void
handle_paint_events (void)
{
	/* FIXME: we need to mask input events correctly here */
	/* Show something coherent */
	while (gtk_events_pending () && !initial_workbook_open_complete)
		gtk_main_iteration_do (FALSE);
}


static void
warn_about_ancient_gnumerics (const char *binary, IOContext *ioc)
{
	struct stat buf;
	time_t now = time (NULL);
	int days = 180;
	const char *sep;

	if (!binary)
		return;

	for (sep = binary; *sep; sep++)
		if (G_IS_DIR_SEPARATOR (*sep))
			break;

	if (binary &&
	    *sep &&
	    g_stat (binary, &buf) != -1 &&
	    buf.st_mtime != -1 &&
	    now - buf.st_mtime > days * 24 * 60 * 60) {
		handle_paint_events ();

		go_cmd_context_error_system (GO_CMD_CONTEXT (ioc),
			_("Thank you for using Gnumeric!\n"
			  "\n"
			  "The version of Gnumeric you are using is quite old\n"
			  "by now.  It is likely that many bugs have been fixed\n"
			  "and that new features have been added in the meantime.\n"
			  "\n"
			  "Please consider upgrading before reporting any bugs.\n"
			  "Consult http://www.gnome.org/projects/gnumeric/ for details.\n"
			  "\n"
			  "-- The Gnumeric Team."));
	}
}

#ifdef WITH_GNOME
#include <libgnome/gnome-program.h>
#include <libgnome/gnome-init.h>
#include <libgnomeui/gnome-ui-init.h>
#include <libgnomeui/gnome-authentication-manager.h>
#include <libgnomevfs/gnome-vfs-init.h>

static GnomeProgram *program;

static void
gnumeric_arg_shutdown (void)
{
	g_object_unref (program);
	program = NULL;
}

static poptContext
gnumeric_arg_parse (int argc, char const *argv [])
{
	poptContext ctx = NULL;
	int i;

	/* no need to init gtk when dumping function info */
	for (i = 0 ; i < argc ; i++)
		if (argv[i] && 0 == strncmp ("--dump-func", argv[i], 11))
			break;

	program = gnome_program_init (PACKAGE, VERSION,
		(i < argc) ? LIBGNOME_MODULE : LIBGNOMEUI_MODULE,
		argc, (char **)argv,
		GNOME_PARAM_APP_PREFIX,		GNUMERIC_PREFIX,
		GNOME_PARAM_APP_SYSCONFDIR,	GNUMERIC_SYSCONFDIR,
		GNOME_PARAM_APP_DATADIR,	gnm_sys_data_dir (),
		GNOME_PARAM_APP_LIBDIR,		gnm_sys_lib_dir (),
		GNOME_PARAM_POPT_TABLE,		gnumeric_popt_options,
		NULL);
	if (i == argc) {		/* Has gui */
		gnome_vfs_init ();
		gnome_authentication_manager_init ();
	}

	g_object_get (G_OBJECT (program),
		GNOME_PARAM_POPT_CONTEXT,	&ctx,
		NULL);
	return ctx;
}
#else
#include <gtk/gtkmain.h>

static poptContext arg_context;

static void
gnumeric_arg_shutdown (void)
{
	poptFreeContext (arg_context);
}

static poptContext
gnumeric_arg_parse (int argc, char const *argv [])
{
	static struct poptOption const gtk_options [] = {
		{ "gdk-debug", '\0', POPT_ARG_STRING, NULL, 0,
		  N_("Gdk debugging flags to set"), N_("FLAGS")},
		{ "gdk-no-debug", '\0', POPT_ARG_STRING, NULL, 0,
		  N_("Gdk debugging flags to unset"), N_("FLAGS")},
		{ "display", '\0', POPT_ARG_STRING, NULL, 0,
		  N_("X display to use"), N_("DISPLAY")},
		{ "screen", '\0', POPT_ARG_INT, NULL, 0,
		  N_("X screen to use"), N_("SCREEN")},
		{ "sync", '\0', POPT_ARG_NONE, NULL, 0,
		  N_("Make X calls synchronous"), NULL},
		{ "name", '\0', POPT_ARG_STRING, NULL, 0,
		  N_("Program name as used by the window manager"), N_("NAME")},
		{ "class", '\0', POPT_ARG_STRING, NULL, 0,
		  N_("Program class as used by the window manager"), N_("CLASS")},
		{ "gtk-debug", '\0', POPT_ARG_STRING, NULL, 0,
		  N_("Gtk+ debugging flags to set"), N_("FLAGS")},
		{ "gtk-no-debug", '\0', POPT_ARG_STRING, NULL, 0,
		  N_("Gtk+ debugging flags to unset"), N_("FLAGS")},
		{ "g-fatal-warnings", '\0', POPT_ARG_NONE, NULL, 0,
		  N_("Make all warnings fatal"), NULL},
		{ "gtk-module", '\0', POPT_ARG_STRING, NULL, 0,
		  N_("Load an additional Gtk module"), N_("MODULE")},
		POPT_TABLEEND
	};
	static struct poptOption const options [] = {
		{ NULL, '\0', POPT_ARG_INTL_DOMAIN, (char *)GETTEXT_PACKAGE, 0, NULL, NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, (poptOption *)gnumeric_popt_options, 0,
		  N_("Gnumeric options"), NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, (poptOption *)gtk_options, 0,
		  N_("GTK+ options"), NULL },
		POPT_TABLEEND
	};

	int i;

#ifdef G_OS_WIN32
	gnumeric_popt_options[1].arg = &gnumeric_lib_dir;
	gnumeric_popt_options[2].arg = &gnumeric_data_dir;
	gnumeric_popt_options[5].arg = &gnumeric_debugging;
	gnumeric_popt_options[6].arg = &dependency_debugging;
	gnumeric_popt_options[7].arg = &expression_sharing_debugging;
	gnumeric_popt_options[8].arg = &print_debugging;
	gnumeric_popt_options[9].arg = &x_geometry;
	gnumeric_popt_options[10].arg = &gnumeric_no_splash;
	gnumeric_popt_options[11].arg = &gnumeric_no_warnings;
	gnumeric_popt_options[12].arg = &immediate_exit_flag;
#endif

	/* no need to init gtk when dumping function info */
	for (i = 0 ; i < argc ; i++)
		if (argv[i] && 0 == strncmp ("--dump-func", argv[i], 11))
			break;
	if (i >= argc)
		gtk_init (&argc, (char ***)&argv);
	else
		g_type_init ();

	arg_context = poptGetContext (PACKAGE, argc, argv, options, 0);
	while (poptGetNextOpt (arg_context) > 0)
		;
	return arg_context;
}
#endif

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
	gnm_gconf_set_active_plugins (active_plugins);
	g_slist_free (active_plugins);
}

static int
cb_kill_wbcg (WorkbookControlGUI *wbcg)
{
	wbcg_close_control (wbcg);
	return FALSE;
}

static void
check_pango_attr_list_splice_bug (void)
{
	PangoAttrList *l1 = pango_attr_list_new ();
	PangoAttrList *l2 = pango_attr_list_new ();
	PangoAttribute *a = pango_attr_weight_new (1000);
	PangoAttrIterator *it;
	gboolean buggy;

	a->start_index = 4;
	a->end_index = 5;
	pango_attr_list_insert (l1, a);

	pango_attr_list_splice (l1, l2, 0, 1);
	pango_attr_list_unref (l2);

	it = pango_attr_list_get_iterator (l1);
	if (pango_attr_iterator_next (it)) {
		gint s, e;
		pango_attr_iterator_range (it, &s, &e);
		buggy = (s != 5 || e != 6);
	} else
		buggy = TRUE;
	pango_attr_iterator_destroy (it);
	pango_attr_list_unref (l1);

	if (buggy)
		g_warning (_("The Pango library present on your system is buggy, see\n"
			     "http://bugzilla.gnome.org/show_bug.cgi?id=316054\n"
			     "Editing rich text therefore does not work well.  Please check\n"
			     "with your distribution if a fixed Pango library is available."));
}

int
main (int argc, char const *argv [])
{
	char const **startup_files;
	gboolean opened_workbook = FALSE;
	gboolean with_gui;
	IOContext *ioc;
	WorkbookView *wbv;
	GSList *wbcgs_to_kill = NULL;
	poptContext ctx;
	gchar const **args = go_shell_argv_to_glib_encoding (argc, argv);
#ifdef G_OS_WIN32
	gboolean has_console = FALSE;
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

	gnm_pre_parse_init (args[0]);

	ctx = gnumeric_arg_parse (argc, args);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	if (gnumeric_show_version) {
		g_print (_("gnumeric version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			 GNUMERIC_VERSION, gnm_sys_data_dir (), gnm_sys_lib_dir ());
		return 0;
	}

	with_gui = !func_def_file && !func_state_file && !split_funcdocs;

	if (with_gui) {
		check_pango_attr_list_splice_bug ();
		gnm_session_init (args[0]);
	}

	/* TODO: Use the ioc.  Do this before calling handle_paint_events */
	gnm_common_init (TRUE);

	if (with_gui) {
		ioc = IO_CONTEXT (g_object_new (TYPE_IO_CONTEXT_GTK, NULL));
		handle_paint_events ();
	} else {
		/* TODO: Make this inconsistency go away */
		GOCmdContext *cc = cmd_context_stderr_new ();
		ioc = gnumeric_io_context_new (cc);
		g_object_unref (cc);
	}

	if (func_state_file)
		return gnm_dump_func_defs (func_state_file, 0);
	if (func_def_file)
		return gnm_dump_func_defs (func_def_file, 1);
	if (split_funcdocs)
		return gnm_dump_func_defs (NULL, 2);

	/* Keep in sync with .desktop file */
	g_set_application_name (_("Gnumeric Spreadsheet"));
 	gnm_plugins_init (GO_CMD_CONTEXT (ioc));

	/* Load selected files */
	if (ctx)
		startup_files = poptGetArgs (ctx);
	else
		startup_files = NULL;

#ifdef WITH_GNOME
	bonobo_activate ();
#endif
	if (startup_files) {
		int i;

		for (i = 0; startup_files [i]; i++)
			;

		gnm_io_context_set_num_files (ioc, i);
		for (i = 0;
		     startup_files [i] && !initial_workbook_open_complete;
		     i++) {
			char *uri = go_shell_arg_to_uri (startup_files[i]);

			if (uri == NULL) {
				g_warning ("Ignoring invalid URI.");
				continue;
			}

			gnm_io_context_processing_file (ioc, uri);
			wbv = wb_view_new_from_uri (uri, NULL, ioc, NULL);
			g_free (uri);

			if (gnumeric_io_error_occurred (ioc) ||
			    gnumeric_io_warning_occurred (ioc)) {
				gnumeric_io_error_display (ioc);
				gnumeric_io_error_clear (ioc);
			}
			if (wbv != NULL) {
				WorkbookControlGUI *wbcg;

				wbcg = WORKBOOK_CONTROL_GUI
					(workbook_control_gui_new (wbv, NULL, NULL));
				sheet_update (wb_view_cur_sheet	(wbv));
  				opened_workbook = TRUE;
				icg_set_transient_for (IO_CONTEXT_GTK (ioc),
						       wbcg_toplevel (wbcg));
				if (immediate_exit_flag)
					wbcgs_to_kill = g_slist_prepend (wbcgs_to_kill,
									   wbcg);
			}
			/* cheesy attempt to keep the ui from freezing during
			   load */
			handle_paint_events ();
			if (icg_get_interrupted (IO_CONTEXT_GTK (ioc)))
				break; /* Don't load any more workbooks */
		}
	}
	/* FIXME: May be we should quit here if we were asked to open
	   files and failed to do so. */

	/* If we were intentionally short circuited exit now */
	if (!initial_workbook_open_complete) {
		initial_workbook_open_complete = TRUE;
		if (!opened_workbook) {
			gint n_of_sheets = gnm_app_prefs->initial_sheet_number;

			workbook_control_gui_new
				(NULL, workbook_new_with_sheets (n_of_sheets), NULL);
			/* cheesy attempt to keep the ui from freezing during load */
			handle_paint_events ();
		}

		if (immediate_exit_flag) {
			GSList *l;
			for (l = wbcgs_to_kill; l; l = l->next)
				g_idle_add ((GSourceFunc)cb_kill_wbcg, l->data);
		} else {
			warn_about_ancient_gnumerics (g_get_prgname(), ioc);
		}
		g_object_unref (ioc);
#ifdef WITH_GNOME
		bonobo_main ();
#else
		gtk_main ();
#endif
	} else {
		g_object_unref (ioc);
		g_slist_foreach (wbcgs_to_kill, (GFunc)cb_kill_wbcg, NULL);
	}

	g_slist_free (wbcgs_to_kill);
	gnumeric_arg_shutdown ();
	store_plugin_state ();
	gnm_shutdown ();

#ifdef WITH_GNOME
	bonobo_ui_debug_shutdown ();
#elif defined(G_OS_WIN32)
	if (has_console) {
		close(1);
		close(2);
		FreeConsole();
	}
#endif
	go_shell_argv_to_glib_encoding_free ();

	return 0;
}
