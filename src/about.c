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
#include "workbook.h"

/*
 * We need to get rid of that so that we will be able
 * to list everybody.  Something like guname would be
 * nice
 */
void
dialog_about (Workbook *wb)
{
        GtkWidget *about, *l, *href, *hbox;
	
        const gchar *authors[] = {
		N_("Miguel de Icaza, main programmer."),
		N_("Sean Atkinson, Functions and X-Base importing."),
		N_("Tom Dyas, Plugin support."),
		N_("Jody Goldberg, Gnumeric hacker."),
		N_("Ross Ihaka, special functions."),
		N_("Jukka-Pekka Iivonen, numerous functions and tools."),
		N_("Jakub Jelinek, Gnumeric hacker."),
		N_("Jon K Hellan, Excel hacker."),
		N_("Chris Lahey, Number format engine."),
		N_("Adrian Likins, Documentation, debugging."),
		N_("Takashi Matsuda, simple text plugin."),
		N_("Federico Mena, Canvas support."),
		N_("Michael Meeks, Excel and OLE2 importing."),
		N_("Mark Probst, Guile support."),
		N_("Rasca, HTML, troff, LaTeX exporters."),
		N_("Vincent Renardias, CSV support, French localisation."),
		N_("Ariel Rios, Guile support."),
		N_("Bruno Unna, Excel bits."),
		N_("Daniel Veillard, XML support."),
		N_("Vladimir Vuksan, financial functions."),
		N_("Morten Welinder, Gnumeric hacker."),
		N_("Gergõ Érdi, Gnumeric hacker."),
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
				 _("(C) 1998-2000 Miguel de Icaza"),
				 authors,
				 NULL,
				 NULL);

	hbox = gtk_hbox_new (TRUE, 0);
	l = gnome_href_new ("http://www.gnome.org/gnumeric",
			    _("Gnumeric Home Page"));
	href = gnome_href_new ("http://www.gnome-support.com/gnumeric",
			       _("Gnumeric Support page"));
	gtk_box_pack_start (GTK_BOX (hbox), l, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), href, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (about)->vbox),
			    hbox, TRUE, FALSE, 0);
	gtk_widget_show_all (hbox);
	
	gnome_dialog_set_parent (GNOME_DIALOG (about), GTK_WINDOW (wb->toplevel));
	gnome_dialog_set_close (GNOME_DIALOG (about), TRUE);
        gtk_widget_show (about);
}
