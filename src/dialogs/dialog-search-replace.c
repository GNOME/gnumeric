/*
 * dialog-search-replace.c:
 *   Dialog for entering a search-and-replace query.
 *
 * Author:
 *   Morten Welinder (terra@diku.dk)
 */
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "search.h"


static const char *error_group[] = {
	"error_fail",
	"error_skip",
	"error_query",
	"error_error",
	"error_string",
	0
};

static const char *search_type_group[] = {
	"search_type_text",
	"search_type_regexp",
	0
};


static int
get_group_value (GladeXML *gui, const char *group[])
{
	int i;
	for (i = 0; group[i]; i++) {
		GtkWidget *w = glade_xml_get_widget (gui, group[i]);
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)))
			return i;
	}
	return -1;
}

static gboolean
is_checked (GladeXML *gui, const char *name)
{
	GtkWidget *w = glade_xml_get_widget (gui, name);
	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
}

static const char *
get_text (GladeXML *gui, const char *name)
{
	GtkWidget *w = glade_xml_get_widget (gui, name);
	return gtk_entry_get_text (GTK_ENTRY (w));
}


/*
 * Dialog
 */
SearchReplace *
dialog_search_replace (WorkbookControlGUI *wbcg)
{
	GladeXML *gui;
	SearchReplace *sr;
	GnomeDialog *dialog;
	int bval;

	g_return_val_if_fail (wbcg != NULL, NULL);

	gui = gnumeric_glade_xml_new (wbcg, "search-replace.glade");
        if (gui == NULL)
                return NULL;

	dialog = GNOME_DIALOG (glade_xml_get_widget (gui, "dialog"));

#if 0
	gnome_dialog_set_default (dialog, BUTTON_CLOSE);
#endif
	gtk_window_set_policy (GTK_WINDOW (dialog), FALSE, TRUE, FALSE);

	gtk_widget_show_all (dialog->vbox);

	while (1) {
		bval = gnumeric_dialog_run (wbcg, dialog);

		if (bval == 0) {
			int i;

			sr = search_replace_new ();

			i = get_group_value (gui, search_type_group);
			sr->is_regexp = (i == 1);

			sr->search_text = g_strdup (get_text (gui, "searchtext"));
			sr->replace_text = g_strdup (get_text (gui, "replacetext"));

			sr->query = is_checked (gui, "query");
			sr->ignore_case = FALSE;  /* Not in gui yet.  */

			sr->replace_strings = is_checked (gui, "replace_string");
			sr->replace_other_values = is_checked (gui, "replace_other");
			sr->replace_expressions = is_checked (gui, "replace_expr");
			sr->replace_comments = is_checked (gui, "replace_comments");

			i = get_group_value (gui, error_group);
			sr->error_behaviour = (i == -1) ? SRE_fail : (SearchReplaceError)i;
		} else {
			sr = NULL;
		}

		if (sr && strlen (sr->search_text) == 0) {
			gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
					 _("You must enter something to search for."));
			search_replace_free (sr);
		} else if (sr &&
			   !sr->replace_strings &&
			   !sr->replace_other_values &&
			   !sr->replace_expressions &&
			   !sr->replace_comments) {
			gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
					 _("You must select some cell types to search."));
			search_replace_free (sr);
			continue;
		} else
			break;
	}

	if (bval != -1)
		gnome_dialog_close (dialog);

	gtk_object_unref (GTK_OBJECT (gui));

	return sr;
}
