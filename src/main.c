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
#include "dump.h"
#include "main.h"

#include "../plugins/excel/boot.h"
#include "../plugins/xbase/boot.h"
#include <glade/glade.h>
#include <glade/glade-xml.h>

#ifdef HAVE_GUILE
#include <libguile.h>
#endif

/* The debugging level */
int gnumeric_debugging = 0;
extern int ms_excel_read_debug;
extern int ms_excel_formula_debug;
extern int ms_excel_color_debug;
extern int ms_excel_chart_debug;

static char *dump_file_name = NULL;
static char **startup_files = NULL;
static char *startup_glade_file = NULL;

poptContext ctx;

const struct poptOption gnumeric_popt_options [] = {
	{ "dump-func-defs", '\0', POPT_ARG_STRING, &dump_file_name, 0,
	  N_("Dumps the function definitions"),   N_("FILE") },
	{ "debug", '\0', POPT_ARG_INT, &gnumeric_debugging, 0,
	  N_("Enables some debugging functions"), N_("LEVEL") },

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
	{ NULL, '\0', 0, NULL, 0 }
};

static void
gnumeric_main (void *closure, int argc, char *argv [])
{
	int i;
	
	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	gnumeric_arg_parse (argc, argv);

	string_init ();
	format_match_init ();
	style_init ();
	format_color_init ();
	cursors_init ();
	global_symbol_init ();
	constants_init ();
	functions_init ();
	expr_name_init ();
	plugins_init ();

	/* The statically linked in file formats */
	xml_init ();
	excel_init ();

	/* Glade */
	glade_gnome_init ();
	if (startup_glade_file)
		glade_xml_new (startup_glade_file, NULL);
	
	if (dump_file_name){
		dump_functions (dump_file_name);
		exit (0);
	}

	startup_files = poptGetArgs (ctx);
	if (startup_files)
		for (i = 0; startup_files [i]; i++) {
			current_workbook = workbook_read (startup_files [i]);
			
			if (current_workbook)
				gtk_widget_show (current_workbook->toplevel);
		}
	poptFreeContext (ctx);
	
	if (current_workbook == NULL){
		current_workbook = workbook_new_with_sheets (1);
		gtk_widget_show (current_workbook->toplevel);
	}

	gtk_main ();

	excel_shutdown ();
	cursors_shutdown ();
	format_match_finish ();
	format_color_shutdown ();
	style_shutdown ();

	gnome_config_drop_all ();
}

#ifdef HAVE_GUILE
int
main (int argc, char *argv [])
{
	scm_boot_guile(argc, argv, gnumeric_main, 0);
	return 0;
}
#else
int
main (int argc, char *argv [])
{
	gnumeric_main(0, argc, argv);
	return 0;
}
#endif
