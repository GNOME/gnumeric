/*
 * Gnumeric, the GNOME spreadsheet.
 *
 * Main file, startup code.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gnome.h>
#include <locale.h>
#include "gnumeric.h"
#include "xml-io.h"
#ifdef ENABLE_BONOBO
#include "bonobo-io.h"
/* DO NOT include embeddable-grid.h.  It causes odd depends in the non-bonobo
 * case */
extern gboolean EmbeddableGridFactory_init (void);
#endif
#include "stf.h"
#include "main.h"
#include "plugin.h"
#include "format.h"
#include "formats.h"
#include "command-context.h"
#include "workbook.h"
#include "workbook-control-gui.h"
#include "workbook-view.h"
#include "sheet-object.h"
#include "number-match.h"
#include "main.h"
#include "expr-name.h"
#include "func.h"
#include "application.h"
#include "print-info.h"
#include "global-gnome-font.h"
#include "auto-format.h"
#include "style.h"
#include "style-color.h"
#include "str.h"
#include "eval.h"

#include <gal/widgets/e-cursors.h>
#include <glade/glade.h>

#ifdef USE_WM_ICONS
#include <libgnomeui/gnome-window-icon.h>
#endif

#ifdef ENABLE_BONOBO
#include <bonobo.h>
#endif

/* The debugging level */
int gnumeric_debugging = 0;
int style_debugging = 0;
int dependency_debugging = 0;
int immediate_exit_flag = 0;
int print_debugging = 0;
gboolean initial_workbook_open_complete = FALSE;
extern gboolean libole2_debug;

static char *dump_file_name = NULL;
static const char **startup_files = NULL;
static int gnumeric_show_version = FALSE;
char *gnumeric_lib_dir = GNUMERIC_LIBDIR;
char *gnumeric_data_dir = GNUMERIC_DATADIR;

poptContext ctx;

const struct poptOption gnumeric_popt_options [] = {
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
	{ "debug_print", '\0', POPT_ARG_INT, &print_debugging, 0,
	  N_("Enables some print debugging behavior"), N_("LEVEL") },

	{ "quit", '\0', POPT_ARG_NONE, &immediate_exit_flag, 0,
	  N_("Exit immediately after loading the selected books (useful for testing)."), NULL },

	{ "debug_ole", '\0', POPT_ARG_NONE,
	    &libole2_debug, 0,
	  N_("Enables extra consistency checking while reading ole files"),
	  NULL  },

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


/*
 * FIXME: We hardcode the GUI command context. Change once we are able
 * to tell whether we are in GUI or not.
 */
static void
gnumeric_main (void *closure, int argc, char *argv [])
{
	gboolean opened_workbook = FALSE;
	WorkbookControl *wbc;
	const char *gnumeric_binary = argv[0];

	/* Make stdout line buffered - we only use it for debug info */
	setvbuf (stdout, NULL, _IOLBF, 0);

	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	/* Force all of the locale segments to update from the environment.
	 * Unless we do this they will default to C
	 */
	setlocale (LC_ALL, "");

	gnumeric_arg_parse (argc, argv);

	if (gnumeric_show_version) {
		printf (_("gnumeric version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			GNUMERIC_VERSION, GNUMERIC_DATADIR, GNUMERIC_LIBDIR);
		return;
	}
#ifdef USE_WM_ICONS
	gnome_window_icon_set_default_from_file (GNOME_ICONDIR"/gnome-gnumeric.png");
#endif

	application_init ();
	dependent_types_init ();
	string_init ();
	style_init ();
	gnumeric_color_init ();
	format_color_init ();
	format_match_init ();
	e_cursors_init ();
	auto_format_init ();
	functions_init ();
	expr_name_init ();
	print_init ();
	sheet_object_register ();

	/* The statically linked in file formats */
	xml_init ();
	stf_init ();
#ifdef ENABLE_BONOBO
	gnumeric_bonobo_io_init ();
#endif

	global_gnome_font_init ();

	/* Ignore Shift for accelerators to avoid problems with different
	 * keyboard layouts that change the shift state of various keys.
	 *
	 * WARNING : This means that Shift-Space is not valid accelerator.
	 */
	gtk_accelerator_set_default_mod_mask (
		gtk_accelerator_get_default_mod_mask() & ~GDK_SHIFT_MASK);

	/* Glade */
	glade_gnome_init ();

	if (dump_file_name) {
		function_dump_defs (dump_file_name);
		exit (0);
	}

#ifdef ENABLE_BONOBO
#if 0
	/* Activate object factories and init connections to POA */
	if (!WorkbookFactory_init ())
		g_warning (_("Could not initialize Workbook factory"));
#endif

	if (!EmbeddableGridFactory_init ())
		g_warning (_("Could not initialize EmbeddableGrid factory"));
#endif

	/* Load selected files */
	if (ctx)
		startup_files = poptGetArgs (ctx);
	else
		startup_files = NULL;

#ifdef ENABLE_BONOBO
	bonobo_activate ();
#endif
 	wbc = workbook_control_gui_new (NULL, NULL);
 	plugins_init (COMMAND_CONTEXT (wbc));
	if (startup_files) {
		int i;
		for (i = 0; startup_files [i]  && !initial_workbook_open_complete ; i++) {
 			if (wb_view_open (wb_control_view (wbc), wbc, startup_files[i])) {
  				opened_workbook = TRUE;
 			}

			handle_paint_events ();
		}
	}

	if (ctx)
		poptFreeContext (ctx);

	/* If we were intentionally short circuited exit now */
	if (!initial_workbook_open_complete && !immediate_exit_flag) {
		initial_workbook_open_complete = TRUE;
		if (!opened_workbook) {
			workbook_sheet_add (wb_control_workbook (wbc),
					    NULL, FALSE);
			handle_paint_events ();
		}

		if (gnumeric_binary) {
			struct stat buf;
			time_t now = time (NULL);
			int days = 180;

			if (stat (gnumeric_binary, &buf) != -1 &&
			    buf.st_mtime != -1 &&
			    now - buf.st_mtime > days * 24 * 60 * 60) {
				gnumeric_error_system (COMMAND_CONTEXT (wbc),
						       _("Thank you for using Gnumeric\n"
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

		gtk_main ();
	}

	plugins_shutdown ();
	print_shutdown ();
	auto_format_shutdown ();
	e_cursors_shutdown ();
	format_match_finish ();
	format_color_shutdown ();
	gnumeric_color_shutdown ();
	style_shutdown ();
	dependent_types_shutdown ();

	global_gnome_font_shutdown ();

	gnome_config_drop_all ();
}

#ifdef HAVE_GUILE
#include <libguile.h>

gboolean
has_gnumeric_been_compiled_with_guile_support (void)
{
	return TRUE;
}

int
main (int argc, char *argv [])
{
#if 0
	int fd;

	/* FIXME:
	 *
	 * We segfault inside scm_boot_guile if any of stdin, stdout or stderr
	 * is missing. Up to gnome-libs 1.0.56, libgnorba closes stdin, so the
	 * segfault *will* happen when gnumeric is activated that way. This fix
	 * will make sure fd 0, 1 and 2 are valid. Enable when we know where
	 * guile init ends up. */

	fd = open("/dev/null", O_RDONLY);
	if (fd == 0)
		fdopen (fd, "r");
	else
		close (fd);
	if (fd <= 2) {
		for (;;) {
			fd = open("/dev/null", O_WRONLY);
			if (fd <= 2)
				fdopen (fd, "w");
			else {
				close (fd);
				break;
			}
		}
	}
#endif

	scm_boot_guile (argc, argv, gnumeric_main, 0);
	return 0;
}
#else
gboolean
has_gnumeric_been_compiled_with_guile_support (void)
{
	return FALSE;
}

int
main (int argc, char *argv [])
{
	gnumeric_main (0, argc, argv);
	return 0;
}
#endif
