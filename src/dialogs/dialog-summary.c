/**
 * dialog-summary.c:  Implements the summary info stuff
 *
 * Author:
 *        Michael Meeks <michael@imaginator.com>
 *
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <workbook.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>

#define SUMMARY_DEBUG 0

static void
summary_get (GladeXML *gui, SummaryInfo *sin)
{
	int lp;

	for (lp = 0; lp < SUMMARY_I_MAX; lp++) {
		SummaryItem *sit;
		gchar *name = g_strconcat ("glade_", summary_item_name[lp], NULL);
		GtkWidget *w = glade_xml_get_widget (gui, name);

		if (!w)
#if SUMMARY_DEBUG > 0
			printf ("Error missing builtin summary name '%s'\n", name);
#else
		;
#endif
		else { /* FIXME: OK so far, but what if it isn't editable ? */
			if (lp == SUMMARY_I_COMMENTS) {
				char *txt = gtk_editable_get_chars (GTK_EDITABLE (w),
							      0, gtk_text_get_length (GTK_TEXT (w)));
				sit = summary_item_new_string (summary_item_name[lp], txt);
				g_free (txt);
			} else {
				const char *txt = gtk_entry_get_text (GTK_ENTRY (w));
				sit = summary_item_new_string (summary_item_name[lp], txt);
			}

			summary_info_add (sin, sit);
		}
		g_free (name);
	}
}

static void
summary_put (GladeXML *gui, SummaryInfo *sin)
{
	GList *l, *m;

	m = l = summary_info_as_list (sin);
	while (l) {
		gchar       *name =  NULL;
		SummaryItem *sit = l->data;
		GtkWidget   *w ;

		if (sit && (sit->type == SUMMARY_STRING)) {
			name = g_strconcat ("glade_", sit->name, NULL);
			w = glade_xml_get_widget (gui, name);
			if (w) {
				gchar *txt = sit->v.txt;

				if (g_strcasecmp (sit->name, summary_item_name[SUMMARY_I_COMMENTS]) == 0) {
					gint p = 0;
					gtk_editable_insert_text (GTK_EDITABLE (w), txt, strlen (txt), &p);
				} else
					gtk_entry_set_text (GTK_ENTRY (w), txt);
			}
			g_free (name);
		}
		l = g_list_next (l);
	}
	g_list_free (m);
}

void
dialog_summary_update (WorkbookControlGUI *wbcg, SummaryInfo *sin)
{
	GladeXML  *gui;
	GtkWidget *dia, *comments;
	int i;
	static char *names[]
	  = {
	     "glade_title",
	     "glade_author",
	     "glade_category",
	     "glade_keywords",
	     "glade_manager",
	     "glade_company"
	    };
	gint v;

	gui = gnumeric_glade_xml_new (wbcg, "summary.glade");
        if (gui == NULL)
                return;

	dia = glade_xml_get_widget (gui, "SummaryInformation");
	if (!dia) {
		printf ("Corrupt file summary.glade\n");
		return;
	}

	for (i = 0; i < (int) (sizeof (names)/sizeof (char *)); i++) {
		GtkWidget *entry;
		entry = glade_xml_get_widget (gui, names[i]);
		gnome_dialog_editable_enters (GNOME_DIALOG (dia),
					      GTK_EDITABLE (entry));
	}
	comments = glade_xml_get_widget (gui, "glade_comments");
	gtk_text_set_word_wrap (GTK_TEXT (comments), TRUE);

	summary_put (gui, sin);

	v = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dia));
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
