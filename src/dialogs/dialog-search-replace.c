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

typedef struct {
	WorkbookControlGUI *wbcg;
	GladeXML *gui;
	GnomeDialog *dialog;
	SearchReplaceDialogCallback cb;
} DialogState;

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

static void
ok_clicked (GtkWidget *widget, DialogState *dd)
{
	GladeXML *gui = dd->gui;
	GnomeDialog *dialog = dd->dialog;
	WorkbookControlGUI *wbcg = dd->wbcg;
	SearchReplaceDialogCallback cb = dd->cb;
	SearchReplace *sr;
	char *err;
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
	sr->preserve_case = is_checked (gui, "preserve_case");
	sr->match_words = is_checked (gui, "match_words");

	sr->replace_strings = is_checked (gui, "replace_string");
	sr->replace_other_values = is_checked (gui, "replace_other");
	sr->replace_expressions = is_checked (gui, "replace_expr");
	sr->replace_comments = is_checked (gui, "replace_comments");

	i = get_group_value (gui, error_group);
	sr->error_behaviour = (i == -1) ? SRE_fail : (SearchReplaceError)i;

	err = search_replace_verify (sr);
	if (err) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR, err);
		g_free (err);
		search_replace_free (sr);
		return;
	} else if (!sr->replace_strings &&
		   !sr->replace_other_values &&
		   !sr->replace_expressions &&
		   !sr->replace_comments) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("You must select some cell types to search."));
		search_replace_free (sr);
		return;
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
	dd = NULL;  /* Destroyed */

	cb (wbcg, sr);
	search_replace_free (sr);
}

static void
cancel_clicked (GtkWidget *widget, DialogState *dd)
{
	GnomeDialog *dialog = dd->dialog;

	gtk_widget_destroy (GTK_WIDGET (dialog));
}


static void
dialog_destroy (GtkWidget *widget, DialogState *dd)
{
	GladeXML *gui = dd->gui;
	gtk_object_unref (GTK_OBJECT (gui));
	memset (dd, 0, sizeof (*dd));
	g_free (dd);
}


static void
non_model_dialog (WorkbookControlGUI *wbcg, GnomeDialog *dialog)
{
	GtkWindow *toplevel = wb_control_gui_toplevel (wbcg);

	if (GTK_WINDOW (dialog)->transient_parent != toplevel)
		gnome_dialog_set_parent (dialog, toplevel);

	gtk_widget_show (GTK_WIDGET (dialog));
}


void
dialog_search_replace (WorkbookControlGUI *wbcg,
		       SearchReplaceDialogCallback cb)
{
	GladeXML *gui;
	GnomeDialog *dialog;
	DialogState *dd;

	g_return_if_fail (wbcg != NULL);

	gui = gnumeric_glade_xml_new (wbcg, "search-replace.glade");
        if (gui == NULL)
                return;

	dialog = GNOME_DIALOG (glade_xml_get_widget (gui, "search_replace_dialog"));

	dd = g_new (DialogState, 1);
	dd->wbcg = wbcg;
	dd->gui = gui;
	dd->cb = cb;
	dd->dialog = dialog;

	gtk_window_set_policy (GTK_WINDOW (dialog), FALSE, TRUE, FALSE);

	gtk_signal_connect (GTK_OBJECT (glade_xml_get_widget (gui, "ok_button")),
			    "clicked",
			    GTK_SIGNAL_FUNC (ok_clicked),
			    dd);
	gtk_signal_connect (GTK_OBJECT (glade_xml_get_widget (gui, "cancel_button")),
			    "clicked",
			    GTK_SIGNAL_FUNC (cancel_clicked),
			    dd);
	gtk_signal_connect (GTK_OBJECT (dialog),
			    "destroy",
			    GTK_SIGNAL_FUNC (dialog_destroy),
			    dd);

	gtk_widget_show_all (dialog->vbox);

	non_model_dialog (wbcg, dialog);
}
