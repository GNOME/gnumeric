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

void
dialog_about (void)
{
        GtkWidget *about;
        const gchar *authors[] = {
		"Miguel de Icaza, main programmer.",
		"Daniel Viellard, XML support.",
		"Chris Lahey, Number format engine.",
		"Tom Dyas, Plugin support.",
		"Federico Mena, Canvas support.",
		NULL
	};

        about = gnome_about_new (_("Gnumeric"), VERSION,
				 "(C) 1998 Miguel de Icaza",
				 authors,
				 _("The GNOME spreadsheet."),
				 NULL);
	gnome_dialog_set_modal (GNOME_DIALOG (about));
	gnome_dialog_set_close (GNOME_DIALOG (about), TRUE);
        gtk_widget_show (about);
}

