#include <gnome.h>
#include "gnumeric.h"

int
main (int argc, char *argv [])
{
	Workbook *wb;

	gnome_init ("Gnumeric", NULL, argc, argv, 0, NULL);
	
	wb = workbook_new_with_sheets (1);
	gtk_widget_show (wb->toplevel);

	gtk_main ();

	return 0;
}
