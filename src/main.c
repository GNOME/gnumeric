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

#ifdef HAVE_GUILE
#include <libguile.h>
#endif

/* The debugging level */
int gnumeric_debugging = 0;

static char *dump_file_name = NULL;
static char **startup_files = NULL;

poptContext ctx;

const struct poptOption gnumeric_popt_options [] = {
	{ "dump-func-defs", '\0', POPT_ARG_STRING, &dump_file_name, 0,
	  N_("Dumps the function definitions"),   N_("FILE") },
	{ "debug", '\0', POPT_ARG_INT, &gnumeric_debugging, 0,
	  N_("Enables some debugging functions"), N_("LEVEL") },
	{ NULL, '\0', 0, NULL, 0 }
};

static void
gnumeric_main (void *closure, int argc, char *argv [])
{
	GList *l;
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
	plugins_init ();

	/* The statically linked in file formats */
	xml_init ();
	excel_init ();
	
	if (dump_file_name){
		dump_functions (dump_file_name);
		exit (1);
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

	cursors_shutdown ();
	format_match_finish ();
	format_color_shutdown ();

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
