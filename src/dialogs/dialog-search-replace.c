/*
 * dialog-search-replace.c:
 *   Dialog for entering a search-and-replace query.
 *
 * Author:
 *   Morten Welinder (terra@diku.dk)
 */
#include <config.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gui-util.h"
#include "dialogs.h"
#include "search.h"
#include "widgets/gnumeric-expr-entry.h"
#include "workbook-edit.h"

#define SEARCH_REPLACE_KEY "search-replace-dialog"

typedef struct {
	WorkbookControlGUI *wbcg;
	GladeXML *gui;
	GnomeDialog *dialog;
	GnumericExprEntry *rangetext;
	SearchDialogCallback cb;
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

static const char *direction_group[] = {
	"row_major",
	"column_major",
	0
};

static gboolean
is_checked (GladeXML *gui, const char *name)
{
	GtkWidget *w = glade_xml_get_widget (gui, name);
	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
}

static void
set_checked (GladeXML *gui, const char *name, gboolean checked)
{
	GtkWidget *w = glade_xml_get_widget (gui, name);
	return gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), checked);
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
	SearchDialogCallback cb = dd->cb;
	SearchReplace *sr;
	char *err;
	int i;

	sr = search_replace_new ();

	sr->search_text = get_text (gui, "searchtext");
	sr->replace_text = get_text (gui, "replacetext");

	i = gnumeric_glade_group_value (gui, search_type_group);
	sr->is_regexp = (i == 1);

	i = gnumeric_glade_group_value (gui, scope_group);
	sr->scope = (i == -1) ? SRS_sheet : (SearchReplaceScope)i;
	sr->range_text = g_strdup (
		gtk_entry_get_text (GTK_ENTRY (dd->rangetext)));

	sr->query = is_checked (gui, "query");
	sr->preserve_case = is_checked (gui, "preserve_case");
	sr->ignore_case = is_checked (gui, "ignore_case");
	sr->match_words = is_checked (gui, "match_words");

	sr->search_strings = is_checked (gui, "search_string");
	sr->search_other_values = is_checked (gui, "search_other");
	sr->search_expressions = is_checked (gui, "search_expr");
	sr->search_comments = is_checked (gui, "search_comments");

	i = gnumeric_glade_group_value (gui, error_group);
	sr->error_behaviour = (i == -1) ? SRE_fail : (SearchReplaceError)i;

	i = gnumeric_glade_group_value (gui, direction_group);
	sr->by_row = (i == 0);

	err = search_replace_verify (sr, TRUE);
	if (err) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR, err);
		g_free (err);
		search_replace_free (sr);
		return;
	} else if (!sr->search_strings &&
		   !sr->search_other_values &&
		   !sr->search_expressions &&
		   !sr->search_comments) {
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
	wbcg_edit_detach_guru (dd->wbcg);
	memset (dd, 0, sizeof (*dd));
	g_free (dd);
}


static void
set_focus (GtkWidget *widget, GtkWidget *focus_widget, DialogState *dd)
{
	if (IS_GNUMERIC_EXPR_ENTRY (focus_widget))
		wbcg_set_entry (dd->wbcg,
				    GNUMERIC_EXPR_ENTRY (focus_widget));
	else
		wbcg_set_entry (dd->wbcg, NULL);

}

static gboolean
range_focused (GtkWidget *widget, GdkEventFocus   *event, DialogState *dd)
{
	GtkWidget *scope_range = glade_xml_get_widget (dd->gui, "scope_range");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scope_range), TRUE);
	return FALSE;
}

static void
non_model_dialog (WorkbookControlGUI *wbcg,
		  GnomeDialog *dialog,
		  const char *key)
{
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (dialog), key);

	gtk_widget_show (GTK_WIDGET (dialog));
}


void
dialog_search_replace (WorkbookControlGUI *wbcg,
		       SearchDialogCallback cb)
{
	GladeXML *gui;
	GnomeDialog *dialog;
	GtkBox *hbox;
	DialogState *dd;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbcg_edit_has_guru (wbcg))
		return;

	if (gnumeric_dialog_raise_if_exists (wbcg, SEARCH_REPLACE_KEY))
		return;

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

	dd->rangetext = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (wbcg));
	gnumeric_expr_entry_set_flags (
		dd->rangetext,
		GNUM_EE_SHEET_OPTIONAL, GNUM_EE_SHEET_OPTIONAL);
	hbox = GTK_BOX (glade_xml_get_widget (gui, "range_hbox"));
	gtk_box_pack_start (hbox, GTK_WIDGET (dd->rangetext),
			    TRUE, TRUE, 0);
	gtk_widget_show (GTK_WIDGET (dd->rangetext));

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
	gtk_signal_connect (GTK_OBJECT (dialog), "set-focus",
			    GTK_SIGNAL_FUNC (set_focus), dd);
	gtk_signal_connect (GTK_OBJECT (dd->rangetext), "focus-in-event",
			    GTK_SIGNAL_FUNC (range_focused), dd);

	gtk_widget_show_all (dialog->vbox);
	gnumeric_expr_entry_set_scg (dd->rangetext,
				     wb_control_gui_cur_sheet (wbcg));
	wbcg_edit_attach_guru (wbcg, GTK_WIDGET (dialog));

	non_model_dialog (wbcg, dialog, SEARCH_REPLACE_KEY);
}

int
dialog_search_replace_query (WorkbookControlGUI *wbcg,
			     SearchReplace *sr,
			     const char *location,
			     const char *old_text,
			     const char *new_text)
{
	GladeXML *gui;
	GnomeDialog *dialog;
	int res;

	g_return_val_if_fail (wbcg != NULL, 0);

	gui = gnumeric_glade_xml_new (wbcg, "search-replace.glade");
        if (gui == NULL)
                return 0;

	dialog = GNOME_DIALOG (glade_xml_get_widget (gui, "query_dialog"));

	gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (gui, "qd_location")),
			    location);
	gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (gui, "qd_old_text")),
			    old_text);
	gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (gui, "qd_new_text")),
			    new_text);
	set_checked (gui, "qd_query", sr->query);

	gtk_window_set_policy (GTK_WINDOW (dialog), FALSE, TRUE, FALSE);

	gtk_widget_show_all (dialog->vbox);

	res = gnumeric_dialog_run (wbcg, dialog);

	/* Unless cancel is pressed, propagate the query setting back.  */
	if (res != -1)
		sr->query = is_checked (gui, "qd_query");

	if (res != -1)
		gtk_widget_destroy (GTK_WIDGET (dialog));

	if (res == 2)
		res = -1;

	return res;
}

/* ------------------------------------------------------------------------- */
