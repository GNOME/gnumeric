/**
 * dialog-summary.c:  Implements the summary info stuff
 *
 * Author:
 *        Michael Meeks <michael@imaginator.com>
 *
 **/
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"

#define SUMMARY_DEBUG 0

static void
summary_get (GladeXML *gui, SummaryInfo *sin)
{
	int lp;

	for (lp = 0; lp < SUMMARY_I_MAX; lp++) {
		SummaryItem *sit;
		gchar *name = summary_item_name[lp];
		GtkWidget *w = glade_xml_get_widget (gui, name);

		if (!w)
#if SUMMARY_DEBUG > 0
			printf ("Error missing builtin summary name '%s'\n", name);
#else
		;
#endif
		else { /* FIXME: OK so far, but what if it isn't editable ? */
			gchar *txt;

			if (lp == SUMMARY_I_COMMENTS)
				txt = gtk_editable_get_chars (GTK_EDITABLE (w),
							      0, gtk_text_get_length (GTK_TEXT (w)));
			else
				txt = gtk_entry_get_text (GTK_ENTRY (w));

			sit = summary_item_new_string (name, txt);
			summary_info_add (sin, sit);
		}
	}
}

static void
summary_put (GladeXML *gui, SummaryInfo *sin)
{
	GList *l, *m;

	m = l = summary_info_as_list (sin);
	while (l) {
		SummaryItem *sit = l->data;
		GtkWidget *w ;

		if (sit && sit->type == SUMMARY_STRING &&
		    (w = glade_xml_get_widget (gui, sit->name))) {
			gchar *txt = sit->v.txt;

			if (g_strcasecmp (sit->name, summary_item_name [SUMMARY_I_COMMENTS]) == 0) {
				gint p = 0;
				gtk_editable_insert_text (GTK_EDITABLE (w), txt, strlen (txt), &p);
			} else
				gtk_entry_set_text (GTK_ENTRY (w), txt);
		}
		l = g_list_next (l);
	}
	g_list_free (m);
}

void
dialog_summary_update (Workbook *wb, SummaryInfo *sin)
{
	GladeXML  *gui = glade_xml_new (GNUMERIC_GLADEDIR "/summary.glade", NULL);
	GtkWidget *dia;
	gint v;

	if (!gui) {
		printf ("Could not find summary.glade\n");
		return;
	}
	
	dia = glade_xml_get_widget (gui, "SummaryInformation");
	if (!dia) {
		printf ("Corrupt file summary.glade\n");
		return;
	}
	
	gnome_dialog_set_parent (GNOME_DIALOG (dia),
				 GTK_WINDOW (wb->toplevel));
	summary_put (gui, sin);

	v = gnome_dialog_run (GNOME_DIALOG (dia));
	if (v == 0)
		summary_get (gui, sin);

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));

	gtk_object_unref (GTK_OBJECT (gui));

#if SUMMARY_DEBUG > 0
	printf ("After update:\n");
	summary_info_dump (sin);
#endif
}
