/*
 * main-application.c: Main entry point for the Gnumeric application
 *
 * Author:
 *   Jon Kåre Hellan <hellan@acm.org>
 *   Morten Welinder <terra@diku.dk>
 *
 * Copyright (C) 2002, Jon Kåre Hellan
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "libgnumeric.h"

#include "command-context.h"
#include "io-context.h"
#include "io-context-gtk.h"
/* TODO: Get rid of this one */
#include "command-context-stderr.h"
#include "workbook-control-gui.h"
#include "workbook-view.h"
#include "plugin.h"
#include "workbook.h"
#include "gui-file.h"
#include "gnumeric-gconf.h"

#include <gtk/gtkmain.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>

#ifdef WITH_BONOBO
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-ui-main.h>
#endif

static int gnumeric_show_version = FALSE;
static char *func_def_file = NULL;
static char *func_state_file = NULL;

int gnumeric_no_splash = FALSE;
char const *gnumeric_lib_dir = GNUMERIC_LIBDIR;
char const *gnumeric_data_dir = GNUMERIC_DATADIR;

const struct poptOption
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

	{ "quit", '\0', POPT_ARG_NONE, &immediate_exit_flag, 0,
	  N_("Exit immediately after loading the selected books (useful for testing)."), NULL },

	{ NULL, '\0', 0, NULL, 0 }
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

	if (binary &&
	    strchr (binary, '/') != NULL &&
	    stat (binary, &buf) != -1 &&
	    buf.st_mtime != -1 &&
	    now - buf.st_mtime > days * 24 * 60 * 60) {
		handle_paint_events ();

		gnumeric_error_system (COMMAND_CONTEXT (ioc),
				       _("Thank you for using Gnumeric!\n"
					 "\n"
					 "The version of Gnumeric you are using is quite old\n"
					 "by now.  It is likely that many bugs have been fixed\n"
					 "and that new features have been added in the meantime.\n"
					 "\n"
					 "Please consider upgrading before reporting any bugs.\n"
					 "Consult http://www.gnumeric.org/ for details.\n"
					 "\n"
					 "-- The Gnumeric Team."));
	}
}


int
main (int argc, char *argv [])
{
	char const **startup_files;
	gboolean opened_workbook = FALSE;
	gboolean with_gui;
	IOContext *ioc;
	WorkbookView *wbv;

	poptContext ctx;

	init_init (argv[0]);

	ctx = gnumeric_arg_parse (argc, argv);

	if (gnumeric_show_version) {
		printf (_("gnumeric version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			GNUMERIC_VERSION, GNUMERIC_DATADIR, GNUMERIC_LIBDIR);
		return 0;
	}

	with_gui = !func_def_file && !func_state_file;
	if (with_gui) {
		ioc = IO_CONTEXT (g_object_new (TYPE_IO_CONTEXT_GTK, NULL));
		handle_paint_events ();
	} else {
		/* TODO: Make this inconsistency go away */
		CommandContextStderr *ccs = command_context_stderr_new ();
		ioc = gnumeric_io_context_new (COMMAND_CONTEXT (ccs));
		g_object_unref (ccs);
	}

	/* TODO: Use the ioc. */
	gnm_common_init ();

	if (func_def_file)
		return gnm_dump_func_defs (func_def_file, TRUE);
	if (func_state_file)
		return gnm_dump_func_defs (func_state_file, FALSE);

 	plugins_init (COMMAND_CONTEXT (ioc));

	/* Load selected files */
	if (ctx)
		startup_files = poptGetArgs (ctx);
	else
		startup_files = NULL;

#ifdef WITH_BONOBO
	bonobo_activate ();
#endif
	if (startup_files) {
		int i;

		for (i = 0; startup_files [i]; i++)
			;
		/* FIXME: What to do for non GUI case? Make set_files_total
		 * an io-context method or branch on GUI/non GUI */
		icg_set_files_total (IO_CONTEXT_GTK (ioc), i);
		for (i = 0;
		     startup_files [i] && !initial_workbook_open_complete;
		     i++) {
			wbv = wb_view_new_from_file (startup_files[i],
						     NULL, ioc);
			icg_inc_files_done (IO_CONTEXT_GTK (ioc));
			if (gnumeric_io_error_occurred (ioc) ||
			    gnumeric_io_warning_occurred (ioc)) {
				gnumeric_io_error_display (ioc);
				gnumeric_io_error_clear (ioc);
			}
			if (wbv != NULL) {
				WorkbookControlGUI *wbcg;
				
				wbcg = WORKBOOK_CONTROL_GUI
					(workbook_control_gui_new (wbv, NULL));
  				opened_workbook = TRUE;
				icg_set_transient_for (IO_CONTEXT_GTK (ioc),
						       wbcg_toplevel (wbcg));
			}
			/* cheesy attempt to keep the ui from freezing during
			   load */
			handle_paint_events ();
		}
	}
	/* FIXME: May be we should quit here if we were asked to open
	   files and failed to do so. */

	/* If we were intentionally short circuited exit now */
	if (!initial_workbook_open_complete && !immediate_exit_flag) {
		initial_workbook_open_complete = TRUE;
		if (!opened_workbook) {
			gint n_of_sheets = gnm_app_prefs->initial_sheet_number;

			workbook_control_gui_new
				(NULL, workbook_new_with_sheets (n_of_sheets));
			/* cheesy attempt to keep the ui from freezing during load */
			handle_paint_events ();
		}

		warn_about_ancient_gnumerics (g_get_prgname(), ioc);
		g_object_unref (ioc);
		gtk_main ();
	}

	gnm_shutdown ();

#ifdef WITH_BONOBO
	bonobo_ui_debug_shutdown ();
#endif

	return 0;
}
