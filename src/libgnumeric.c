#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "xml-io.h"
#include "plugin.h"
#include "format.h"
#include "cursors.h"
#include "number-match.h"
#include "dump.h"

/* If set, the file to load at startup time */
static GList *startup_files;

static char *dump_file_name;

enum {
	DUMP_FUNCS_KEY = -1
};

static struct argp_option argp_options [] = {
	{ "dump-func-defs",  DUMP_FUNCS_KEY, N_("FILE"),  0, N_("Dumps the functions definitions") },
	{ NULL,     0,     NULL,          0, NULL, 0 },
};

static error_t
parse_an_arg (int key, char *arg, struct argp_state *state)
{
	switch (key){
	case DUMP_FUNCS_KEY:
		dump_file_name = arg;
		break;
		
	case ARGP_KEY_INIT:
	case ARGP_KEY_FINI:
		return 0;

	default:
		if (arg)
			startup_files = g_list_prepend (startup_files, arg);
	}
	
	return 0;
}

static struct argp parser = {
	argp_options, parse_an_arg, NULL, NULL, NULL, NULL, NULL
};

int
main (int argc, char *argv [])
{
	GList *l;
	
	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	gnome_init ("Gnumeric", &parser, argc, argv, 0, NULL);

	string_init ();
	format_match_init ();
	style_init ();
	format_color_init ();
	cursors_init ();
	symbol_init ();
	constants_init ();
	functions_init ();
	plugins_init ();

	if (dump_file_name){
		dump_functions (dump_file_name);
		exit (1);
	}

	/* Load any specified files on the command line */
	for (l = startup_files; l; l = l->next){
		current_workbook = gnumericReadXmlWorkbook (l->data);

		if (current_workbook)
			gtk_widget_show (current_workbook->toplevel);
		
	}
	g_list_free (startup_files);
	
	if (current_workbook == NULL){
		current_workbook = workbook_new_with_sheets (1);
		gtk_widget_show (current_workbook->toplevel);
	}

	gtk_main ();

	cursors_shutdown ();
	format_match_finish ();
	format_color_shutdown ();
	return 0;
}

