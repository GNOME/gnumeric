/*
 * about.c: Shows the contributors to Gnumeric.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "dialogs.h"

/*
 * We need to get rid of that so that we will be able
 * to list everybody.  Somethign like guname would be
 * nice
 */
void
dialog_about (Workbook *wb)
{
        GtkWidget *about;
        const gchar *authors[] = {
		N_("Miguel de Icaza, main programmer."),
		N_("Daniel Veillard, XML support."),
		N_("Chris Lahey, Number format engine."),
		N_("Tom Dyas, Plugin support."),
		N_("Federico Mena, Canvas support."),
		N_("Adrian Likins, Documentation, debugging"),
		N_("Jakub Jelinek, Gnumeric hacker"),
		N_("Michael Meeks, Excel and OLE2 importing"),
		N_("Sean Atkinson, Excel functions"),
		N_("Bruno Unna, Excel code"),
		N_("Mark Probst, Guile support"),
		N_("Vincent Renardias, CSV support"),
		N_("Vladimir Vuksan, financial functions"),
		N_("Takashi Matsuda, simple text plugin"),
		N_("Jukka-Pekka Iivonen, numerous functions"),
		N_("Morten Welinder, Gnumeric hacker"),
		NULL
	};

#ifdef ENABLE_NLS
	{
 	    int i;
	    
	    for (i = 0; authors[i] != NULL; i++){
		    authors [i] = _(authors [i]);
	    }
	}
#endif

        about = gnome_about_new (_("Gnumeric"), VERSION,
				 "(C) 1998-1999 Miguel de Icaza",
				 authors,
				 _("The GNOME spreadsheet.\n"
				   "http://www.gnome.org/gnumeric"),
				 NULL);
	gnome_dialog_set_parent (GNOME_DIALOG (about), GTK_WINDOW (wb->toplevel));
	gnome_dialog_set_close (GNOME_DIALOG (about), TRUE);
        gtk_widget_show (about);
}
