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
dialog_about (void)
{
        GtkWidget *about;
        const gchar *authors[] = {
		"Miguel de Icaza, main programmer.",
		"Daniel Veillard, XML support.",
		"Chris Lahey, Number format engine.",
		"Tom Dyas, Plugin support.",
		"Federico Mena, Canvas support.",
		"Adrian Likins, Documentation, debugging",
		NULL
	};

        about = gnome_about_new (_("Gnumeric"), VERSION,
				 "(C) 1998 Miguel de Icaza",
				 authors,
				 _("The GNOME spreadsheet.\n"
				   "http://www.gnome.org/gnumeric"),
				 NULL);
	gtk_window_set_modal (GTK_WINDOW (about), TRUE);
	gnome_dialog_set_close (GNOME_DIALOG (about), TRUE);
        gtk_widget_show (about);
}

