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

static const char *scope_group[] = {
	"scope_workbook",
	"scope_sheet",
	"scope_range",
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

static char *
get_text (GladeXML *gui, const char *name)
{
	GtkWidget *w = glade_xml_get_widget (gui, name);
	return g_strdup (gtk_entry_get_text (GTK_ENTRY (w)));
}


/*
 * Dialog.  Returns a SearchReplace object that the users wants to search
 * for, or NULL if the search is cancelled.
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
		char *err;
		bval = gnumeric_dialog_run (wbcg, dialog);

		if (bval == 0) {
			int i;

			sr = search_replace_new ();

			sr->search_text = get_text (gui, "searchtext");
			sr->replace_text = get_text (gui, "replacetext");

			i = get_group_value (gui, search_type_group);
			sr->is_regexp = (i == 1);

			i = get_group_value (gui, scope_group);
			sr->scope = (i == -1) ? SRS_sheet : (SearchReplaceScope)i;
			sr->range_text = get_text (gui, "rangetext");

			sr->query = is_checked (gui, "query");
			sr->ignore_case = is_checked (gui, "ignore_case");

			sr->replace_strings = is_checked (gui, "replace_string");
			sr->replace_other_values = is_checked (gui, "replace_other");
			sr->replace_expressions = is_checked (gui, "replace_expr");
			sr->replace_comments = is_checked (gui, "replace_comments");

			i = get_group_value (gui, error_group);
			sr->error_behaviour = (i == -1) ? SRE_fail : (SearchReplaceError)i;
		} else {
			sr = NULL;
		}

		if (sr && (err = search_replace_verify (sr))) {
			gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR, err);
			g_free (err);
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
