#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "xml-io.h"

int
main (int argc, char *argv [])
{
	gnome_init ("Gnumeric", NULL, argc, argv, 0, NULL);

	string_init ();
	style_init ();
	symbol_init ();
	constants_init ();
	functions_init ();
	
	current_workbook = workbook_new_with_sheets (1);
	gtk_widget_show (current_workbook->toplevel);

	gtk_main ();

	return 0;
}

