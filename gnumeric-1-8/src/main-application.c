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
#endif

#include "command-context.h"
#include <goffice/app/io-context.h>
#include "io-context-gtk.h"
/* TODO: Get rid of this one */
#include "command-context-stderr.h"
#include "wbc-gtk-impl.h"
#include "workbook-view.h"
#include <goffice/app/go-plugin.h>
#include "workbook.h"
#include "gui-file.h"
#include "gnumeric-gconf.h"
#include "gnumeric-paths.h"
#include "session.h"
#include "sheet.h"
#include "gutils.h"
#include "gnm-plugin.h"

#include <gtk/gtkmain.h>
#include <glib/gstdio.h>
#include <goffice/utils/go-file.h>
#include <goffice/app/go-cmd-context.h>

#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <locale.h>

#ifdef GNM_WITH_GNOME
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-ui-main.h>
#include <libgnome/gnome-program.h>
#include <libgnome/gnome-init.h>
#include <libgnomeui/gnome-ui-init.h>
#include <libgnomeui/gnome-authentication-manager.h>
#endif

#ifdef GNM_USE_HILDON
#include <libosso.h>
#endif

static gboolean split_funcdocs = FALSE;
static gboolean immediate_exit_flag = FALSE;
static gboolean gnumeric_no_splash = FALSE;
static gboolean gnumeric_no_warnings = FALSE;
static gchar  *func_def_file = NULL;
static gchar  *func_state_file = NULL;
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

static void
warn_about_ancient_gnumerics (char const *binary, IOContext *ioc)
{
	struct stat buf;
	time_t now = time (NULL);
	int days = 180;
	char const *sep;

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

	ocontext = g_option_context_new ("[FILE ...]");
	g_option_context_add_main_entries (ocontext, gnumeric_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());

#ifdef GNM_WITH_GNOME
#ifndef GNOME_PARAM_GOPTION_CONTEXT
	/*
	 * Bummer.  We cannot make gnome_program_init handle our args so
	 * we do it ourselves.  That, in turn, means we don't handle
	 * libgnome[ui]'s args.
	 *
	 * Upgrade to libgnome 2.13 or better to solve this.
	 */
	if (!funcdump)
		g_option_context_add_group (ocontext, gtk_get_option_group (TRUE));
	g_option_context_parse (ocontext, &argc, &argv, &error);
#endif

	if (!error) {
		program = (GObject *)
			gnome_program_init (PACKAGE, VERSION,
					    funcdump ? LIBGNOME_MODULE : LIBGNOMEUI_MODULE,
					    argc, argv,
					    GNOME_PARAM_APP_PREFIX,		GNUMERIC_PREFIX,
					    GNOME_PARAM_APP_SYSCONFDIR,		GNUMERIC_SYSCONFDIR,
					    GNOME_PARAM_APP_DATADIR,		gnm_sys_data_dir (),
					    GNOME_PARAM_APP_LIBDIR,		gnm_sys_lib_dir (),
#ifdef GNOME_PARAM_GOPTION_CONTEXT
					    GNOME_PARAM_GOPTION_CONTEXT,	ocontext,
#endif
					    NULL);
#ifdef GNOME_PARAM_GOPTION_CONTEXT
		ocontext = NULL;
#endif
	}

#else /* therefore not gnome */
	if (!funcdump)
		g_option_context_add_group (ocontext, gtk_get_option_group (TRUE));
	g_option_context_parse (ocontext, &argc, &argv, &error);
#endif

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
#ifdef GNM_WITH_GNOME
		gnome_authentication_manager_init ();
#endif
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
	gnm_gconf_set_active_plugins (active_plugins);
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
main (int argc, char const **argv)
{
	gboolean opened_workbook = FALSE;
	gboolean with_gui;
	IOContext *ioc;
	WorkbookView *wbv;
	GSList *wbcgs_to_kill = NULL;
#ifdef GNM_USE_HILDON
	osso_context_t * osso_context;
#endif

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

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

#ifdef GNM_USE_HILDON
	osso_context = osso_initialize ("gnumeric", GNM_VERSION_FULL, TRUE, NULL);
#endif

	gnumeric_arg_parse (argc, (char **)argv);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	with_gui = !func_def_file && !func_state_file && !split_funcdocs;

	if (with_gui) {
		check_pango_attr_list_splice_bug ();
		gnm_session_init (argv[0]);
	}

	gnm_init (TRUE);

	if (with_gui) {
		ioc = IO_CONTEXT (g_object_new (TYPE_IO_CONTEXT_GTK, NULL));
		handle_paint_events ();
		pathetic_qt_workaround ();
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

#ifdef GNM_WITH_GNOME
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
				WBCGtk *wbcg;

				workbook_update_history (wb_view_get_workbook (wbv));

				wbcg = wbc_gtk_new (wbv, NULL, NULL, geometry);
				geometry = NULL;
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
	/* FIXME: Maybe we should quit here if we were asked to open
	   files and failed to do so. */

	/* If we were intentionally short circuited exit now */
	if (!initial_workbook_open_complete) {
		initial_workbook_open_complete = TRUE;
		if (!opened_workbook) {
			gint n_of_sheets = gnm_app_prefs->initial_sheet_number;
			wbc_gtk_new (NULL,
				workbook_new_with_sheets (n_of_sheets),
				NULL, NULL);
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

		g_idle_add ((GSourceFunc)pathetic_qt_workaround, NULL);
#ifdef GNM_WITH_GNOME
		bonobo_main ();
#else
		gtk_main ();
#endif
	} else {
		g_object_unref (ioc);
		g_slist_foreach (wbcgs_to_kill, (GFunc)cb_kill_wbcg, NULL);
	}

#ifdef GNM_USE_HILDON
	osso_deinitialize (osso_context);
#endif

	g_slist_free (wbcgs_to_kill);
	gnumeric_arg_shutdown ();
	store_plugin_state ();
	gnm_shutdown ();

#ifdef GNM_WITH_GNOME
	bonobo_ui_debug_shutdown ();
#elif defined(G_OS_WIN32)
	if (has_console) {
		close(1);
		close(2);
		FreeConsole();
	}
#endif

	gnm_pre_parse_shutdown ();

	return 0;
}

#if 0
/* A handy way of telling valgrind to produce good leak reports.  */
gboolean g_module_close (GModule *module) { return FALSE; }
#endif
