/*
 * Gnumeric, the GNOME spreadsheet.
 *
 * Main file, startup code.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "xml-io.h"
#include "plugin.h"
#include "format.h"
#include "cursors.h"
#include "number-match.h"
#include "main.h"
#include "func.h"
#include "application.h"
#include "print-info.h"
#include "global-gnome-font.h"

#include "../plugins/excel/boot.h"
#include <glade/glade.h>
#include <glade/glade-xml.h>

#ifdef HAVE_GUILE
#include <libguile.h>
#endif

/* The debugging level */
int gnumeric_debugging = 0;
int style_debugging = 0;
int dependency_debugging = 0;
int immediate_exit_flag = 0;
extern int ms_excel_read_debug;
extern int ms_excel_formula_debug;
extern int ms_excel_color_debug;
extern int ms_excel_chart_debug;
extern int ms_excel_write_debug;
extern gboolean libole2_debug;

static char *dump_file_name = NULL;
static char *startup_glade_file = NULL;
static const char **startup_files = NULL;

poptContext ctx;

const struct poptOption gnumeric_popt_options [] = {
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

static void
gnumeric_main (void *closure, int argc, char *argv [])
{
	gboolean opened_workbook = FALSE;
	int i;
	Workbook *new_book;

	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	gnumeric_arg_parse (argc, argv);

	application_init ();
	string_init ();
	format_match_init ();
	style_init ();
	format_color_init ();
	cursors_init ();
	global_symbol_init ();
	constants_init ();
	functions_init ();
	expr_name_init ();
	print_init ();
	plugins_init ();

	/* The statically linked in file formats */
	xml_init ();
	excel_init ();

	global_gnome_font_init ();

	/* Glade */
	glade_gnome_init ();
	if (startup_glade_file)
		glade_xml_new (startup_glade_file, NULL);

	if (dump_file_name) {
		function_dump_defs (dump_file_name);
		exit (0);
	}

	/* Create an empty workbook, just in case we end up loading no files.
	   NOTE: we need to create it here because workbook_do_destroy does
	   special things if the number of workbooks reaches zero.  (And this
	   would happen if loading the first file specified failed.)  */
	new_book = workbook_new_with_sheets (1);

	startup_files = poptGetArgs (ctx);
	if (startup_files)
		for (i = 0; startup_files [i]; i++) {
			Workbook *new_book = workbook_read (startup_files [i]);

			if (new_book) {
				opened_workbook = TRUE;
				gtk_widget_show (new_book->toplevel);
			}

			while (gtk_events_pending ()) /* Show something coherent */
				gtk_main_iteration ();
		}
	poptFreeContext (ctx);

	if (opened_workbook) {
#ifdef ENABLE_BONOBO
		gnome_object_unref (GNOME_OBJECT (new_book));
#else
		gtk_object_unref (GTK_OBJECT (new_book));
#endif
	} else {
		gtk_widget_show (new_book->toplevel);
	}

#ifdef ENABLE_BONOBO
	bonobo_activate ();
#endif

	{
		Range a, b;
		GList *l;
		
		a.start.col = 1;
		a.start.row = 1;
		a.end.col = 1;
		a.end.row = 1;

		b = a;

		l = range_fragment (&a, &b);

		printf ("Split ranges:\n");
		for (; l; l = l->next){
			Range *r = l->data;
			
			printf ("  %d %d %d %d\n",
				r->start.col, r->start.row,
				r->end.col, r->end.row);

			range_dump (l->data);
		}
	}
	
	if (!immediate_exit_flag)
		gtk_main ();

	excel_shutdown ();
	print_shutdown ();
	cursors_shutdown ();
	format_match_finish ();
	format_color_shutdown ();
	style_shutdown ();

	global_gnome_font_shutdown ();

	gnome_config_drop_all ();
}

#ifdef HAVE_GUILE
int
main (int argc, char *argv [])
{
	scm_boot_guile (argc, argv, gnumeric_main, 0);
	return 0;
}
#else
int
main (int argc, char *argv [])
{
	gnumeric_main (0, argc, argv);
	return 0;
}
#endif
