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
#include "gnumeric-util.h"

#define ABOUT_KEY          "about-dialog"

/* Object data is to make sure we don't pop up more than one copy. When
   closing, we remove the data */
static void
cb_closed (GtkWidget *button, WorkbookControlGUI *wbcg)
{
	g_return_if_fail (gtk_object_get_data (GTK_OBJECT (wbcg), ABOUT_KEY) != NULL);

	gtk_object_remove_data (GTK_OBJECT (wbcg), ABOUT_KEY);
}

/*
 * We need to get rid of that so that we will be able
 * to list everybody.  Something like guname would be
 * nice
 */
void
dialog_about (WorkbookControlGUI *wbcg)
{
        GtkWidget *about, *l, *href, *hbox;

        const gchar *authors[] = {
		N_("Miguel de Icaza, creator."),
		N_("Jody Goldberg, maintainer."),
		N_("Sean Atkinson, functions and X-Base importing."),
		N_("Grandma Chema Celorio Tester and sheet copy."),
		N_("Frank Chiulli OLE support."),
		N_("Kenneth Christiansen, i18n, misc stuff."),
		N_("Zbigniew Chyla, plugin system, i18n."),
		N_("Tom Dyas, plugin support."),
		N_("Gergõ Érdi, Gnumeric hacker."),
		N_("Jon K. Hellan, Gnumeric hacker."),
		N_("Ross Ihaka, special functions."),
		N_("Jukka-Pekka Iivonen, numerous functions and tools."),
		N_("Jakub Jelinek, Gnumeric hacker."),
		N_("Chris Lahey, number format engine."),
		N_("Adrian Likins, documentation, debugging."),
		N_("Takashi Matsuda, original text plugin."),
		N_("Michael Meeks, Excel and OLE2 importing."),
		N_("Federico M. Quintero, canvas support."),
		N_("Mark Probst, Guile support."),
		N_("Rasca, HTML, troff, LaTeX exporters."),
		N_("Vincent Renardias, original CSV support, French localization."),
		N_("Ariel Rios, Guile support."),
		N_("Arturo Tena OLE support."),
		N_("Almer S. Tigelaar, Gnumeric hacker."),
		N_("Bruno Unna, Excel bits."),
		N_("Daniel Veillard, XML support."),
		N_("Vladimir Vuksan, financial functions."),
		N_("Morten Welinder, Gnumeric hacker."),
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
	/* Ensure we only pop up one copy per workbook */
	about = gtk_object_get_data (GTK_OBJECT (wbcg), ABOUT_KEY);
	if (about && GNOME_IS_ABOUT (about)) {
		gdk_window_raise (about->window);
		return;
	}

        about = gnome_about_new (_("Gnumeric"), VERSION,
				 _("(C) 1998-2001 Miguel de Icaza"),
				 authors,
				 NULL,
				 NULL);

	hbox = gtk_hbox_new (TRUE, 0);
	l = gnome_href_new ("http://www.gnumeric.org",
			    _("Gnumeric Home Page"));
	href = gnome_href_new ("http://www.ximian.com/apps/gnumeric.php3",
			       _("Contract Support"));
	gtk_box_pack_start (GTK_BOX (hbox), l, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), href, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (about)->vbox),
			    hbox, TRUE, FALSE, 0);
	gtk_widget_show_all (hbox);

	gtk_object_set_data (GTK_OBJECT (wbcg), ABOUT_KEY, about);

	gtk_signal_connect (
		GTK_OBJECT (about), "close",
		GTK_SIGNAL_FUNC (cb_closed), wbcg);

	/* Close on click, close with parent */
	gnumeric_dialog_show (wbcg, GNOME_DIALOG (about), TRUE, TRUE);
}
