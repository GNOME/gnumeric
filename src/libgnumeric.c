#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "xml-io.h"

/* If set, the file to load at startup time */
static char *startup_file;

static struct argp_option argp_options [] = {
	{ "file",   'f',   N_("FILE"),    0, N_("FIle to load at startup"), 0 },
	{ NULL,     0,     NULL,          0, NULL, 0 },
};

static error_t
parse_an_arg (int key, char *arg, struct argp_state *state)
{
	switch (key){
	case 'f':
		startup_file = arg;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static struct argp parser = {
	argp_options, parse_an_arg, NULL, NULL, NULL, NULL, NULL
};

int
main (int argc, char *argv [])
{
	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);
	
	gnome_init ("Gnumeric", &parser, argc, argv, 0, NULL);

	string_init ();
	style_init ();
	symbol_init ();
	constants_init ();
	functions_init ();

	if (startup_file)
		current_workbook = gnumericReadXmlWorkbook (startup_file);

	if (current_workbook == NULL)
		current_workbook = workbook_new_with_sheets (1);

	gtk_widget_show (current_workbook->toplevel);

	gtk_main ();

	return 0;
}

