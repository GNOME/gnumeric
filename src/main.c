#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "xml-io.h"

int
main (int argc, char *argv [])
{
	gnome_init ("Gnumeric", NULL, argc, argv, 0, NULL);

	style_init ();
	symbol_init ();
	currentWorkbook = workbook_new_with_sheets (1);
	gtk_widget_show (currentWorkbook->toplevel);

	gtk_main ();

	return 0;
}

