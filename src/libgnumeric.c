#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "xml-io.h"
#include "plugin.h"
#include "format.h"
#include "cursors.h"
#include "number-match.h"
#include "dump.h"

static char *dump_file_name = NULL;

static const struct poptOption options[] = {
  {"dump-func-defs", '\0', POPT_ARG_STRING, &dump_file_name, 0, N_("Dumps the function definitions"), N_("FILE")},
  {NULL, '\0', 0, NULL, 0}
};

int
main (int argc, char *argv [])
{
	GList *l;
	poptContext ctx;
	int i;
	/* If set, the file to load at startup time */
	char **startup_files = NULL;
	
	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	gnome_init_with_popt_table ("gnumeric", VERSION, argc, argv,
				    options, 0, &ctx);

	string_init ();
	format_match_init ();
	style_init ();
	format_color_init ();
	cursors_init ();
	global_symbol_init ();
	constants_init ();
	functions_init ();
	plugins_init ();

	if (dump_file_name){
		dump_functions (dump_file_name);
		exit (1);
	}

	startup_files = poptGetArgs(ctx);
	if(startup_files)
		for(i = 0; startup_files[i]; i++) {
		  current_workbook = gnumericReadXmlWorkbook (startup_files[i]);
	  
		  if (current_workbook)
		    gtk_widget_show (current_workbook->toplevel);
		}
	poptFreeContext(ctx);
	
	if (current_workbook == NULL){
		current_workbook = workbook_new_with_sheets (1);
		gtk_widget_show (current_workbook->toplevel);
	}

	gtk_main ();

	cursors_shutdown ();
	format_match_finish ();
	format_color_shutdown ();

	gnome_config_drop_all ();
	return 0;
}

