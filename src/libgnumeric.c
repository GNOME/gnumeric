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
#include "gnumeric.h"
#include "xml-io.h"
#include "stf.h"
#include "main.h"
#include "plugin.h"
#include "format.h"
#include "workbook.h"
#include "cursors.h"
#include "number-match.h"
#include "main.h"
#include "expr-name.h"
#include "func.h"
#include "application.h"
#include "print-info.h"
#include "global-gnome-font.h"
#include "auto-format.h"

#include "../plugins/excel/boot.h"
#include <glade/glade.h>
#include <glade/glade-xml.h>

#ifdef HAVE_GUILE
#include <libguile.h>
#endif

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
gboolean initial_worbook_open_complete = FALSE;
extern int ms_excel_read_debug;
extern int ms_excel_formula_debug;
extern int ms_excel_color_debug;
extern int ms_excel_chart_debug;
extern int ms_excel_write_debug;
extern int ms_excel_object_debug;
extern gboolean libole2_debug;

static char *dump_file_name = NULL;
static char *startup_glade_file = NULL;
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

	{ "debug_styles", '\0', POPT_ARG_INT, &style_debugging, 0,
	  N_("Enables some style related debugging functions"), N_("LEVEL") },
	{ "debug_deps", '\0', POPT_ARG_INT, &dependency_debugging, 0,
	  N_("Enables some dependency related debugging functions"), N_("LEVEL") },

	{ "quit", '\0', POPT_ARG_NONE, &immediate_exit_flag, 0,
	  N_("Exit immediately after loading the selected books (useful for testing)."), NULL },

	{ "debug_excel_read", '\0', POPT_ARG_INT,
	    &ms_excel_read_debug, 0,
	  N_("Enables debugging mesgs while reading excel workbooks"),
	  N_("LEVEL") },
	{ "debug_excel_formulas", '\0', POPT_ARG_INT,
	    &ms_excel_formula_debug, 0,
	  N_("Enables debugging mesgs while reading excel functions"),
	  N_("LEVEL") },
	{ "debug_excel_color", '\0', POPT_ARG_INT,
	    &ms_excel_color_debug, 0,
	  N_("Enables debugging mesgs while reading excel colours & patterns"),
	  N_("LEVEL") },
	{ "debug_excel_objects", '\0', POPT_ARG_INT,
	    &ms_excel_object_debug, 0,
	  N_("Enables debugging mesgs while reading excel objects"),
	  N_("LEVEL") },
	{ "debug_excel_chart", '\0', POPT_ARG_INT,
	    &ms_excel_chart_debug, 0,
	  N_("Enables debugging mesgs while reading excel charts"),
	  N_("LEVEL") },
	{ "debug_excel_write", '\0', POPT_ARG_INT,
	    &ms_excel_write_debug, 0,
	  N_("Enables debugging mesgs while reading excel workbooks"),
	  N_("LEVEL") },
	{ "debug_ole", '\0', POPT_ARG_NONE,
	    &libole2_debug, 0,
	  N_("Enables extra consistancy checking while reading ole files"),
	  NULL  },

	{ NULL, '\0', 0, NULL, 0 }
};

#include "ranges.h"

/*
 * FIXME: We hardcode the GUI command context. Change once we are able
 * to tell whether we are in GUI or not.
 */
static void
gnumeric_main (void *closure, int argc, char *argv [])
{
	gboolean opened_workbook = FALSE;
	int i;
	Workbook *new_book;
	CommandContext *context; 

	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	gnumeric_arg_parse (argc, argv);

	if (gnumeric_show_version) {
		printf (_("gnumeric version %s\ndatadir := %s\nlibdir := %s\n"),
			GNUMERIC_VERSION, GNUMERIC_DATADIR, GNUMERIC_LIBDIR);
		return;
	}
#ifdef USE_WM_ICONS
	gnome_window_icon_set_default_from_file (GNOME_ICONDIR"/gnome-gnumeric.png");
#endif
	/* For reporting errors before we have an application window */
	context = workbook_command_context_gui (NULL);

	application_init ();
	string_init ();
	style_init ();
	format_match_init ();
	format_color_init ();
	cursors_init ();
	global_symbol_init ();
	constants_init ();
	auto_format_init ();
	functions_init ();
	expr_name_init ();
	print_init ();
	plugins_init (context);

	/* The statically linked in file formats */
	xml_init ();
	excel_init ();
	stf_init ();

	global_gnome_font_init ();

	/* Glade */
	glade_gnome_init ();
	if (startup_glade_file)
		glade_xml_new (startup_glade_file, NULL);

	if (dump_file_name) {
		function_dump_defs (dump_file_name);
		exit (0);
	}

	if (ctx)
		startup_files = poptGetArgs (ctx);
	else
		startup_files = NULL;

	if (startup_files)
		for (i = 0; startup_files [i]; i++) {
			Workbook *new_book = workbook_read (context,
							    startup_files [i]);

			if (new_book) {
				opened_workbook = TRUE;
				gtk_widget_show (new_book->toplevel);
			}

			/* FIXME: we need to mask input events correctly here */
			while (gtk_events_pending ()) /* Show something coherent */
				gtk_main_iteration ();
		}
	if (ctx)
		poptFreeContext (ctx);

	if (!opened_workbook) {
		new_book = workbook_new_with_sheets (1);
#if 0
		workbook_style_test (new_book);
#endif
		gtk_widget_show (new_book->toplevel);
	}
	initial_worbook_open_complete = TRUE;

#ifdef ENABLE_BONOBO
	bonobo_activate ();
#endif

	if (!immediate_exit_flag)
		gtk_main ();

	excel_shutdown ();
	print_shutdown ();
	auto_format_shutdown ();
	cursors_shutdown ();
	format_match_finish ();
	format_color_shutdown ();
	style_shutdown ();

	global_gnome_font_shutdown ();

	gnome_config_drop_all ();
}

#ifdef HAVE_GUILE
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
