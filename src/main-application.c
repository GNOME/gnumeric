/*
 * main-application.c: Main entry point for the Gnumeric application
 *
 * Author:
 *   Jon Kåre Hellan <hellan@acm.org>
 *
 * Copyright (C) 2002, Jon Kåre Hellan
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "libgnumeric.h"

#include "command-context.h"
#include "workbook-control-gui.h"
#include "workbook-view.h"
#include "plugin.h"
#include "workbook.h"
#include "gnumeric-gconf.h"

#include <libgnome/gnome-i18n.h>
#include <gtk/gtkmain.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

static	int gnumeric_show_version = FALSE;
static	char *dump_file_name = NULL;

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

	{ "dump-func-defs", '\0', POPT_ARG_STRING, &dump_file_name, 0,
	  N_("Dumps the function definitions"),   N_("FILE") },

	{ "debug", '\0', POPT_ARG_INT, &gnumeric_debugging, 0,
	  N_("Enables some debugging functions"), N_("LEVEL") },

	{ "debug_deps", '\0', POPT_ARG_INT, &dependency_debugging, 0,
	  N_("Enables some dependency related debugging functions"), N_("LEVEL") },
	{ "debug_share", '\0', POPT_ARG_INT, &expression_sharing_debugging, 0,
	  N_("Enables some debugging functions for expression sharing"), N_("LEVEL") },
	{ "debug_print", '\0', POPT_ARG_INT, &print_debugging, 0,
	  N_("Enables some print debugging behavior"), N_("LEVEL") },

	{ "geometry", 'g', POPT_ARG_STRING, &x_geometry, 0,
	  N_("Specify the size and location of the initial window"), N_("WIDTHxHEIGHT+XOFF+YOFF")
	},

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
warn_about_ancient_gnumerics (const char *binary, WorkbookControl *wbc)
{
	struct stat buf;
	time_t now = time (NULL);
	int days = 180;

	if (binary &&
	    stat (binary, &buf) != -1 &&
	    buf.st_mtime != -1 &&
	    now - buf.st_mtime > days * 24 * 60 * 60) {
		handle_paint_events ();

		gnumeric_error_system (COMMAND_CONTEXT (wbc),
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
	WorkbookControl *wbc;

	poptContext ctx;

	init_init (argv[0]);

	ctx = gnumeric_arg_parse (argc, argv);

	if (gnumeric_show_version) {
		printf (_("gnumeric version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			GNUMERIC_VERSION, GNUMERIC_DATADIR, GNUMERIC_LIBDIR);
		return 0;
	}

	gnm_common_init ();

	if (dump_file_name)
		return gnm_dump_func_defs (dump_file_name); 

	/* Load selected files */
	if (ctx)
		startup_files = poptGetArgs (ctx);
	else
		startup_files = NULL;

#ifdef WITH_BONOBO
	bonobo_activate ();
#endif
 	wbc = workbook_control_gui_new (NULL, NULL);

	/* TODO : make a dialog based command context and do this earlier.  We
	 * should not arbitrarily be using the 1st workbook as the place to
	 * link errors or status.
	 *
	 * plugin init should be earlier too.
	 */
 	plugins_init (COMMAND_CONTEXT (wbc));
	if (startup_files) {
		int i;
		for (i = 0; startup_files [i]  && !initial_workbook_open_complete ; i++) {
 			if (wb_view_open (startup_files[i], wbc, TRUE, NULL))
  				opened_workbook = TRUE;

			/* cheesy attempt to keep the ui from freezing during load */
			handle_paint_events ();
		}
	}

	/* If we were intentionally short circuited exit now */
	if (!initial_workbook_open_complete && !immediate_exit_flag) {
		initial_workbook_open_complete = TRUE;
		if (!opened_workbook) {
			gint n_of_sheets = gnm_gconf_get_initial_sheet_number ();
			while (n_of_sheets--)
				workbook_sheet_add (wb_control_workbook (wbc),
						    NULL, FALSE);

			/* cheesy attempt to keep the ui from freezing during load */
			handle_paint_events ();
		}

		warn_about_ancient_gnumerics (g_get_prgname(), wbc);

		gtk_main ();
	}

	gnm_shutdown ();

	return 0;
}
